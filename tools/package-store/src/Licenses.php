<?php
namespace App;

final class Licenses
{
    public const NAMES = [
        'BSD-3-Clause' => 'BSD 3-Clause License',
        'MIT' => 'MIT License',
        'Apache-2.0' => 'Apache License 2.0',
        'CC0-1.0' => 'Creative Commons Zero 1.0 Universal',
    ];

    public static function find(string $id): ?array
    {
        if (!isset(self::NAMES[$id])) { return null; }
        return ['id' => $id, 'name' => self::NAMES[$id],
            'source' => 'https://spdx.org/licenses/'.$id.'.html',
            'text' => file_get_contents(dirname(__DIR__).'/licenses/'.$id.'.txt')];
    }
}
