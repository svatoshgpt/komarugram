"""Lets people with a KomaruGram status change it from the client.

The client sends "/badge <emoji id> <caption>" or "/badge reset" to this bot.
The bot checks that the sender is in statuses.txt of the gist and writes the
line to custom.txt there, which the clients read over statuses.txt.

Run with BOT_TOKEN (from @BotFather) and GITHUB_TOKEN (a token with the gist
scope) in the environment. Put the bot's username into bot.txt of the gist.
"""
import json
import os
import re
import time
import urllib.error
import urllib.parse
import urllib.request

GIST_ID = os.environ.get('GIST_ID', '300f51e5b67854aa23ab5b592bca0aa8')
BOT_TOKEN = os.environ['BOT_TOKEN']
GITHUB_TOKEN = os.environ['GITHUB_TOKEN']
CAPTION_LIMIT = 180
PERIOD = re.compile(r'^\d+s$')


def call(url, payload=None, headers=None, method=None):
    data = json.dumps(payload).encode() if payload is not None else None
    request = urllib.request.Request(url, data=data, method=method)
    request.add_header('Content-Type', 'application/json')
    request.add_header('User-Agent', 'KomaruGramBadgeBot')
    for name, value in (headers or {}).items():
        request.add_header(name, value)
    with urllib.request.urlopen(request, timeout=70) as response:
        return json.load(response)


def telegram(method, **params):
    return call(f'https://api.telegram.org/bot{BOT_TOKEN}/{method}', params)


def gist(payload=None):
    return call(
        f'https://api.github.com/gists/{GIST_ID}',
        payload,
        {'Authorization': f'Bearer {GITHUB_TOKEN}'},
        'PATCH' if payload is not None else 'GET')


def file_text(files, name):
    entry = files.get(name)
    if not entry:
        return ''
    if entry.get('truncated'):
        with urllib.request.urlopen(entry['raw_url'], timeout=70) as response:
            return response.read().decode()
    return entry.get('content') or ''


def line_id(line):
    head = line.split(',', 1)[0].strip()
    return int(head) if head.isdigit() else 0


def clean_caption(text):
    text = ' '.join(text.split())[:CAPTION_LIMIT]
    parts = text.split(',')
    # A caption that looks like a rotation period would be read as one.
    if PERIOD.match(parts[0].strip()) or PERIOD.match(parts[-1].strip()):
        text += '.'
    return text


def handle(user_id, text):
    match = re.match(r'^/badge(?:@\w+)?\s+(.*)$', text.strip(), re.S)
    if not match:
        return 'Значок меняется в KomaruGram: нажмите на свой значок.'
    files = gist()['files']
    allowed = {line_id(line) for line in file_text(files, 'statuses.txt').splitlines()}
    if user_id not in allowed:
        return 'У вас нет кастомного значка KomaruGram.'
    lines = [
        line for line in file_text(files, 'custom.txt').splitlines()
        if line.strip() and line_id(line) != user_id
    ]
    argument = match.group(1).strip()
    if argument == 'reset':
        reply = 'Значок сброшен.'
    else:
        emoji, _, caption = argument.partition(' ')
        if not emoji.isdigit() or int(emoji) <= 0:
            return 'Не понял эмодзи.'
        caption = clean_caption(caption)
        lines.append(f'{user_id},{int(emoji)}' + (f',{caption}' if caption else ''))
        reply = 'Значок сохранён. У всех обновится через несколько минут.'
    # A gist file cannot be empty, so a comment stays in it.
    content = '\n'.join(lines) + '\n' if lines else '# empty\n'
    gist({'files': {'custom.txt': {'content': content}}})
    return reply


def main():
    offset = 0
    while True:
        try:
            updates = telegram('getUpdates', offset=offset, timeout=50)['result']
        except (urllib.error.URLError, TimeoutError, ValueError) as error:
            print('getUpdates failed:', error)
            time.sleep(5)
            continue
        for update in updates:
            offset = update['update_id'] + 1
            message = update.get('message') or {}
            chat = message.get('chat') or {}
            sender = message.get('from') or {}
            if chat.get('type') != 'private' or not message.get('text'):
                continue
            try:
                reply = handle(sender['id'], message['text'])
            except Exception as error:
                print('failed for', sender.get('id'), error)
                reply = 'Не получилось, попробуйте позже.'
            try:
                telegram('sendMessage', chat_id=chat['id'], text=reply)
            except Exception as error:
                print('sendMessage failed:', error)


if __name__ == '__main__':
    main()
