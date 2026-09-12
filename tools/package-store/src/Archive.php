<?php
namespace App;

final class Archive
{
    public static function inspect(string $path): array
    {
        if (!is_file($path) || filesize($path) > 512 * 1024 * 1024) {
            throw new \InvalidArgumentException('Missing or oversized archive.');
        }
        $stream = fopen($path, 'rb');
        try {
            if (fread($stream, 21) !== "DEMI-PROJECT-PACKAGE\n") {
                // The format magic is 21 bytes including its newline.
                throw new \InvalidArgumentException('Not a Demi project package.');
            }
            $lengthBytes = fread($stream, 8);
            if (strlen($lengthBytes) !== 8) {
                throw new \InvalidArgumentException('Truncated header.');
            }
            $length = unpack('P', $lengthBytes)[1];
            if ($length < 2 || $length > 8 * 1024 * 1024) {
                throw new \InvalidArgumentException('Invalid header length.');
            }
            $header = json_decode(fread($stream, $length), true, 512, JSON_THROW_ON_ERROR);
            $manifest = $header['manifest'] ?? [];
            if (($header['format_version'] ?? null) !== 1 || ($header['package_type'] ?? '') !== 'DemiProjectPackage' ||
                ($manifest['format_version'] ?? null) !== 1 ||
                !preg_match(Catalog::NAME, $manifest['name'] ?? '') || str_contains($manifest['name'], '..') ||
                !preg_match(Catalog::VERSION, $manifest['version'] ?? '') ||
                !is_array($manifest['files'] ?? null) || !is_array($header['files'] ?? null) || count($header['files']) > 10000) {
                throw new \InvalidArgumentException('Invalid package identity or header.');
            }
            $seen = [];
            foreach ($header['files'] as $file) {
                $name = $file['path'] ?? '';
                $size = $file['size'] ?? -1;
                if (!is_string($name) || !preg_match('~^[^/\\\\\x00]+(?:/[^/\\\\\x00]+)*$~D', $name) ||
                    array_intersect(explode('/', $name), ['.', '..']) || isset($seen[$name]) ||
                    !is_int($size) || $size < 0 || $size > 64*1024*1024) {
                    throw new \InvalidArgumentException('Unsafe or duplicate archive entry.');
                }
                $seen[$name] = true;
                $hash = hash_init('sha256');
                $left = $size;
                while ($left > 0) {
                    $chunk = fread($stream, min($left, 65536));
                    if ($chunk === '' || $chunk === false) {
                        throw new \InvalidArgumentException('Truncated archive.');
                    }
                    hash_update($hash, $chunk);
                    $left -= strlen($chunk);
                }
                if ('sha256:'.hash_final($hash) !== ($file['hash'] ?? '')) {
                    throw new \InvalidArgumentException('Archive checksum mismatch.');
                }
            }
            $declared = $manifest['files'];
            sort($declared);
            $actual = array_keys($seen);
            sort($actual);
            if ($declared !== $actual || fread($stream, 1) !== '') {
                throw new \InvalidArgumentException('Undeclared archive content.');
            }
            return $manifest;
        } finally {
            fclose($stream);
        }
    }
}
