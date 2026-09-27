<?php
namespace App;

final class Archive
{
    private const MAGIC = "DEMI-PROJECT-PACKAGE\n";
    private const MAX_ARCHIVE = 512 * 1024 * 1024;
    private const MAX_HEADER = 8 * 1024 * 1024;
    private const MAX_PREVIEW = 1024 * 1024;

    public static function inspect(string $path): array
    {
        [$stream, $header, $length] = self::openHeader($path);
        try {
            $entries = self::index($header, ftell($stream), $length);
            foreach ($entries as $entry) {
                self::seek($stream, $entry['offset']);
                $hash = hash_init('sha256');
                $left = $entry['size'];
                while ($left > 0) {
                    $chunk = fread($stream, min($left, 65536));
                    if ($chunk === '' || $chunk === false) {
                        throw new \InvalidArgumentException('Truncated archive.');
                    }
                    hash_update($hash, $chunk);
                    $left -= strlen($chunk);
                }
                if ('sha256:'.hash_final($hash) !== $entry['hash']) {
                    throw new \InvalidArgumentException('Archive checksum mismatch.');
                }
            }
            return $header['manifest'];
        } finally {
            fclose($stream);
        }
    }

    /** Return a bounded root README for web preview; oversized READMEs have no preview. */
    public static function readReadme(string $path): ?array
    {
        [$stream, $header, $length] = self::openHeader($path);
        try {
            $entries = self::index($header, ftell($stream), $length);
            foreach (['readme.md', 'readme.markdown', 'readme.txt', 'readme'] as $wanted) {
                foreach ($entries as $entry) {
                    if (strtolower($entry['path']) !== $wanted) {
                        continue;
                    }
                    if ($entry['size'] > self::MAX_PREVIEW) {
                        return null;
                    }
                    self::seek($stream, $entry['offset']);
                    $text = self::readExactly($stream, $entry['size']);
                    if ('sha256:'.hash('sha256', $text) !== $entry['hash']) {
                        throw new \InvalidArgumentException('Archive checksum mismatch.');
                    }
                    return ['path' => $entry['path'], 'text' => $text];
                }
            }
            return null;
        } finally {
            fclose($stream);
        }
    }

    private static function openHeader(string $path): array
    {
        if (!is_file($path)) {
            throw new \InvalidArgumentException('Missing or oversized archive.');
        }
        $stream = @fopen($path, 'rb');
        if ($stream === false) {
            throw new \InvalidArgumentException('Cannot open archive.');
        }
        try {
            $stat = fstat($stream);
            $length = $stat['size'] ?? null;
            if (!is_int($length) || $length > self::MAX_ARCHIVE) {
                throw new \InvalidArgumentException('Missing or oversized archive.');
            }
            if (self::readExactly($stream, strlen(self::MAGIC)) !== self::MAGIC) {
                throw new \InvalidArgumentException('Not a Demi project package.');
            }
            $headerLength = unpack('P', self::readExactly($stream, 8))[1];
            if ($headerLength < 2 || $headerLength > self::MAX_HEADER) {
                throw new \InvalidArgumentException('Invalid header length.');
            }
            try {
                $header = json_decode(self::readExactly($stream, $headerLength), true, 512, JSON_THROW_ON_ERROR);
            } catch (\JsonException $error) {
                throw new \InvalidArgumentException('Invalid archive header JSON.', 0, $error);
            }
            $manifest = $header['manifest'] ?? null;
            if (!is_array($header) || !is_array($manifest) ||
                ($header['format_version'] ?? null) !== 1 || ($header['package_type'] ?? null) !== 'DemiProjectPackage' ||
                ($manifest['format_version'] ?? null) !== 1 ||
                !is_string($manifest['name'] ?? null) || !preg_match(Catalog::NAME, $manifest['name']) ||
                str_contains($manifest['name'], '..') || !is_string($manifest['version'] ?? null) ||
                !preg_match(Catalog::VERSION, $manifest['version']) ||
                !is_array($manifest['files'] ?? null) || !array_is_list($manifest['files']) ||
                !is_array($header['files'] ?? null) || !array_is_list($header['files']) ||
                count($header['files']) > 10000) {
                throw new \InvalidArgumentException('Invalid package identity or header.');
            }
            return [$stream, $header, $length];
        } catch (\Throwable $error) {
            fclose($stream);
            throw $error;
        }
    }

    private static function index(array $header, int $offset, int $length): array
    {
        $entries = [];
        $seen = [];
        foreach ($header['files'] as $file) {
            $name = $file['path'] ?? null;
            $size = $file['size'] ?? null;
            $hash = $file['hash'] ?? null;
            if (!is_string($name) || !preg_match('~^[^/\\\\\x00]+(?:/[^/\\\\\x00]+)*$~D', $name) ||
                array_intersect(explode('/', $name), ['.', '..']) || isset($seen[$name]) ||
                !is_int($size) || $size < 0 || $size > 64 * 1024 * 1024 ||
                !is_string($hash) || !preg_match('/^sha256:[a-f0-9]{64}$/D', $hash)) {
                throw new \InvalidArgumentException('Unsafe or invalid archive entry.');
            }
            $seen[$name] = true;
            if ($size > $length - $offset) {
                throw new \InvalidArgumentException('Truncated archive.');
            }
            $entries[] = ['path' => $name, 'size' => $size, 'hash' => $hash, 'offset' => $offset];
            $offset += $size;
        }
        $declared = $header['manifest']['files'];
        if (count($declared) !== count($seen) ||
            count(array_filter($declared, 'is_string')) !== count($declared)) {
            throw new \InvalidArgumentException('Undeclared archive content.');
        }
        sort($declared);
        $actual = array_keys($seen);
        sort($actual);
        if ($declared !== $actual || $offset !== $length) {
            throw new \InvalidArgumentException('Undeclared archive content.');
        }
        return $entries;
    }

    private static function seek($stream, int $offset): void
    {
        if (fseek($stream, $offset, SEEK_SET) !== 0) {
            throw new \InvalidArgumentException('Cannot seek archive entry.');
        }
    }

    private static function readExactly($stream, int $length): string
    {
        $result = '';
        while (strlen($result) < $length) {
            $chunk = fread($stream, $length - strlen($result));
            if ($chunk === false || $chunk === '') {
                throw new \InvalidArgumentException('Truncated archive.');
            }
            $result .= $chunk;
        }
        return $result;
    }
}
