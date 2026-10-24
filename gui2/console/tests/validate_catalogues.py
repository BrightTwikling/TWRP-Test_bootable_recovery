#!/usr/bin/env python3
"""Validate language_pack field order and console formats without compiling."""
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[3]
LANGUAGES = ('en', 'zh_CN', 'zh_TW', 'ru')


def language_fields():
    header = (ROOT / 'gui2/i18n/i18n.h').read_text()
    body = header.split('struct language_pack {', 1)[1].split('};', 1)[0]
    fields = []
    for kind, name, count in re.findall(
            r'(const char\*|action_text)\s+(\w+)(?:\[(\d+)\])?;', body):
        count = int(count or 1)
        if kind == 'action_text':
            fields.extend(f'{name}[{index}].{member}' for index in range(count)
                          for member in ('title', 'summary', 'detail'))
        elif count > 1:
            fields.extend(f'{name}[{index}]' for index in range(count))
        else:
            fields.append(name)
    assert len(fields) == len(set(fields)), 'Duplicate language_pack fields'
    return fields


def bindings():
    source = (ROOT / 'gui2/i18n/i18n.cpp').read_text()
    pairs = re.findall(r'\{"([^"\n]+)", &language_pack::(\w+)},', source)
    keys = [key for key, member in pairs]
    assert keys == sorted(set(keys)), 'Message bindings must be sorted and unique'
    members = [member for key, member in pairs]
    assert len(members) == len(set(members)), 'Duplicate message member bindings'
    assert all(member in language_fields() for member in members), 'Unknown bound field'
    return dict(pairs)


def load_catalogue(language):
    filenames = {'en': 'en.cpp', 'zh_CN': 'zh_cn.cpp', 'zh_TW': 'zh_tw.cpp', 'ru': 'ru.cpp'}
    path = ROOT / 'gui2/i18n' / filenames[language]
    source = path.read_text()
    expected = bindings()
    initializer = source.split('= {', 1)[1].split('\n};', 1)[0]
    initializer = re.sub(
        r'"(?:[^"\\]|\\.)*"|//[^\n]*|/\*.*?\*/',
        lambda match: '' if match[0].startswith(('//', '/*')) else match[0],
        initializer, flags=re.DOTALL)
    values = re.findall(r'"(?:[^"\\]|\\.)*"', initializer)
    fields = language_fields()
    assert len(values) == len(fields), f'{path}: initializer count differs from language_pack'
    actual = dict(zip(fields, values))
    assert fields[-len(expected):] == list(expected.values()), (
        'Recovery fields must follow binding order at the end of language_pack')
    result = {key: json.loads(actual[member]) for key, member in expected.items()}
    assert all(result.values()), f'{path}: empty message translation'
    return result


def signature(pattern):
    result = {}
    cursor = sequential = 0
    modes = set()
    while cursor < len(pattern):
        if pattern[cursor] != '%':
            cursor += 1
            continue
        if pattern.startswith('%%', cursor):
            cursor += 2
            continue
        match = re.match(r'%(?:(\d+)\$)?(ll|l|z)?([sdiu])', pattern[cursor:])
        assert match, f'Unsupported format at {cursor}: {pattern}'
        if match[1]:
            index = int(match[1]) - 1
            modes.add('position')
        else:
            index = sequential
            sequential += 1
            modes.add('sequence')
        assert index >= 0, f'Invalid argument position: {pattern}'
        assert not (match[2] and match[3] == 's'), f'Wide strings are unsupported: {pattern}'
        kind = (('d' if match[3] == 'i' else match[3]), match[2] or '')
        assert index not in result or result[index] == kind, f'Conflicting argument types: {pattern}'
        result[index] = kind
        cursor += len(match[0])
    assert len(modes) <= 1, f'Mixed positional/sequential arguments: {pattern}'
    assert not re.search(r'\{\d*\}', pattern), f'Unconverted placeholder: {pattern}'
    return result


def main():
    catalogues = {language: load_catalogue(language) for language in LANGUAGES}
    english = catalogues['en']
    expected = {key: signature(value) for key, value in english.items()}
    for language, messages in catalogues.items():
        assert messages.keys() == english.keys(), f'{language}: message keys differ from English'
        for key, value in messages.items():
            assert signature(value) == expected[key], f'{language}: incompatible format for {key}'
        print(f'{language}: {len(messages)} messages, matching format signatures')


if __name__ == '__main__':
    main()
