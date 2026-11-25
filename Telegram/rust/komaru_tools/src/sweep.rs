//! Deleting big trees with every core: the CI runner frees disk space and
//! prunes the libraries cache this way.

use std::fs;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::{Condvar, Mutex};

pub struct Outcome {
    pub files: usize,
    pub failed: usize,
    pub stopped: bool,
}

struct Queue {
    folders: Vec<PathBuf>,
    busy: usize,
}

fn remove_file(path: &Path) -> bool {
    if fs::remove_file(path).is_ok() {
        return true;
    }
    // Windows refuses to delete a read-only file.
    if let Ok(meta) = fs::symlink_metadata(path) {
        let mut permissions = meta.permissions();
        #[allow(clippy::permissions_set_readonly_false)]
        permissions.set_readonly(false);
        let _ = fs::set_permissions(path, permissions);
    }
    fs::remove_file(path).is_ok()
}

fn remove_link(path: &Path) -> bool {
    // A directory link on Windows goes away as a directory.
    fs::remove_file(path).is_ok() || fs::remove_dir(path).is_ok()
}

fn depth(path: &Path) -> usize {
    path.components().count()
}

/// Walks `roots` on all cores and deletes every file `doomed` picks, then
/// every folder left empty, deepest first; `keep_roots` spares the roots.
/// Links are deleted when picked, never followed. Once `stop` says so, no
/// further folder is opened.
pub fn sweep(
    roots: &[PathBuf],
    doomed: &(dyn Fn(&Path, &fs::Metadata) -> bool + Sync),
    keep_roots: bool,
    stop: Option<&(dyn Fn() -> bool + Sync)>,
) -> Outcome {
    let queue = Mutex::new(Queue { folders: roots.to_vec(), busy: 0 });
    let wake = Condvar::new();
    let folders = Mutex::new(Vec::new());
    let (files, failed) = (AtomicUsize::new(0), AtomicUsize::new(0));
    let late = || stop.is_some_and(|stop| stop());
    let threads = std::thread::available_parallelism().map_or(4, |n| n.get() * 2);

    std::thread::scope(|scope| {
        for _ in 0..threads {
            scope.spawn(|| loop {
                let folder = {
                    let mut state = queue.lock().unwrap();
                    loop {
                        if late() {
                            state.folders.clear();
                        }
                        if let Some(folder) = state.folders.pop() {
                            state.busy += 1;
                            break Some(folder);
                        }
                        if state.busy == 0 {
                            break None;
                        }
                        state = wake.wait(state).unwrap();
                    }
                };
                let Some(folder) = folder else {
                    wake.notify_all();
                    return;
                };
                let mut found = vec![];
                if let Ok(entries) = fs::read_dir(&folder) {
                    for entry in entries.flatten() {
                        let path = entry.path();
                        let Ok(meta) = fs::symlink_metadata(&path) else {
                            continue;
                        };
                        if meta.is_dir() {
                            found.push(path);
                        } else if doomed(&path, &meta) {
                            let removed = if meta.file_type().is_symlink() {
                                remove_link(&path)
                            } else {
                                remove_file(&path)
                            };
                            let counter = if removed { &files } else { &failed };
                            counter.fetch_add(1, Ordering::Relaxed);
                        }
                    }
                }
                folders.lock().unwrap().push(folder);
                let mut state = queue.lock().unwrap();
                state.folders.extend(found);
                state.busy -= 1;
                wake.notify_all();
            });
        }
    });

    let mut folders = folders.into_inner().unwrap();
    if keep_roots {
        folders.retain(|f| !roots.contains(f));
    }
    folders.sort_by_key(|f| std::cmp::Reverse(depth(f)));
    // A folder fails to go while it still holds kept files: that is fine.
    for folder in folders {
        let _ = fs::remove_dir(&folder);
    }
    Outcome {
        files: files.into_inner(),
        failed: failed.into_inner(),
        stopped: late(),
    }
}

/// Free space on the volume holding `path`, in bytes.
#[cfg(windows)]
pub fn free_space(path: &Path) -> Option<u64> {
    use std::os::windows::ffi::OsStrExt;

    extern "system" {
        fn GetDiskFreeSpaceExW(
            directory: *const u16,
            available: *mut u64,
            total: *mut u64,
            free: *mut u64,
        ) -> i32;
    }
    let wide: Vec<u16> = path.as_os_str().encode_wide().chain(Some(0)).collect();
    let mut available = 0;
    let ok = unsafe {
        GetDiskFreeSpaceExW(
            wide.as_ptr(),
            &mut available,
            std::ptr::null_mut(),
            std::ptr::null_mut(),
        )
    };
    (ok != 0).then_some(available)
}

#[cfg(not(windows))]
pub fn free_space(_path: &Path) -> Option<u64> {
    None
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sweeps_what_it_is_told() {
        let root = std::env::temp_dir().join(format!("komaru_sweep_{}", std::process::id()));
        let _ = fs::remove_dir_all(&root);
        for i in 0..20 {
            let folder = root.join(format!("a{i}/b/c"));
            fs::create_dir_all(&folder).unwrap();
            fs::write(folder.join("keep.h"), "x").unwrap();
            fs::write(folder.join("drop.obj"), "x").unwrap();
        }
        fs::create_dir_all(root.join("empty/deeper")).unwrap();
        let drop = |path: &Path, _: &fs::Metadata| {
            path.extension().is_some_and(|e| e == "obj")
        };
        let outcome = sweep(&[root.clone()], &drop, true, None);
        assert_eq!((outcome.files, outcome.failed), (20, 0));
        assert!(root.join("a7/b/c/keep.h").exists());
        assert!(!root.join("a7/b/c/drop.obj").exists());
        assert!(!root.join("empty").exists());

        let outcome = sweep(&[root.clone()], &|_: &Path, _: &fs::Metadata| true, false, None);
        assert_eq!(outcome.files, 20);
        assert!(!root.exists());
    }

    #[test]
    fn stops_when_told() {
        let root = std::env::temp_dir().join(format!("komaru_stop_{}", std::process::id()));
        let _ = fs::remove_dir_all(&root);
        fs::create_dir_all(root.join("a")).unwrap();
        fs::write(root.join("a/drop.obj"), "x").unwrap();
        let all = |_: &Path, _: &fs::Metadata| true;
        let outcome = sweep(std::slice::from_ref(&root), &all, true, Some(&|| true));
        assert_eq!(outcome.files, 0);
        assert!(outcome.stopped);
        assert!(root.join("a/drop.obj").exists());
        fs::remove_dir_all(&root).unwrap();
    }
}
