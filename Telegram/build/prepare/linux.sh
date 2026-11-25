#!/bin/bash

set -e
FullExecPath=$PWD
pushd `dirname $0` > /dev/null
FullScriptPath=`pwd`
popd > /dev/null

Repo=$FullScriptPath/../../..
cargo run -q --release --locked \
    --manifest-path $Repo/Telegram/rust/komaru_tools/Cargo.toml -- \
    ci dockerfile --template $Repo/Telegram/build/docker/centos_env/Dockerfile \
    | DOCKER_BUILDKIT=1 docker build -t komarugram:centos_env -
cd $FullExecPath
