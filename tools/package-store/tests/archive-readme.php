<?php
require dirname(__DIR__).'/src/Catalog.php';
require dirname(__DIR__).'/src/Archive.php';

use App\Archive;

$file = tempnam(sys_get_temp_dir(), 'demi-readme-test-');
$check = static function(bool $ok, string $message): void {
    if (!$ok) throw new RuntimeException($message);
};
$rejects = static function(callable $call, string $message) use ($check): void {
    try {
        $call();
    } catch (InvalidArgumentException) {
        return;
    }
    $check(false, $message);
};
$package = static function(array $contents, ?array $index = null, ?array $manifestFiles = null): string {
    $index ??= array_map(
        static fn (string $name, string $text): array => [
            'path' => $name, 'size' => strlen($text), 'hash' => 'sha256:'.hash('sha256', $text)
        ],
        array_keys($contents), array_values($contents)
    );
    $manifest = ['format_version' => 1, 'name' => 'test.asset', 'version' => '1.0.0',
        'engine' => '*', 'dependencies' => (object) [], 'public_modules' => [],
        'files' => $manifestFiles ?? array_keys($contents), 'exported_events' => []];
    $header = json_encode(['format_version' => 1, 'package_type' => 'DemiProjectPackage',
        'manifest' => $manifest, 'files' => $index], JSON_THROW_ON_ERROR);
    return "DEMI-PROJECT-PACKAGE\n".pack('P', strlen($header)).$header.implode('', $contents);
};
$read = static function(string $bytes) use ($file): ?array {
    file_put_contents($file, $bytes);
    return Archive::readReadme($file);
};

try {
    $contents = ['src/module.lua' => 'code', 'README.txt' => 'plain',
        'ReadMe.markdown' => 'long', 'readme.MD' => "# Hello\n"];
    $check($read($package($contents)) === ['path' => 'readme.MD', 'text' => "# Hello\n"],
        'Root Markdown README wins regardless of case or index position');
    $check(Archive::inspect($file)['name'] === 'test.asset', 'Full inspection shares the valid header index');
    $check($read($package(['README.txt' => 'text', 'README.markdown' => 'markdown'])) ===
        ['path' => 'README.markdown', 'text' => 'markdown'], 'Markdown extension priority');
    $check($read($package(['README' => 'bare', 'README.txt' => 'text'])) ===
        ['path' => 'README.txt', 'text' => 'text'], 'Text extension priority');
    $check($read($package(['README' => 'bare'])) === ['path' => 'README', 'text' => 'bare'],
        'Bare README');
    $check($read($package(['docs/README.md' => 'nested', 'src/main.lua' => 'code'])) === null,
        'Nested README is not a root README');
    $check($read($package(['README.md' => str_repeat('a', 1024 * 1024)]))['text'] ===
        str_repeat('a', 1024 * 1024), 'One MiB preview allowed');
    $check($read($package(['README.md' => str_repeat('a', 1024 * 1024 + 1)])) === null,
        'Oversized README is unavailable for web preview');

    $uncorrupted = $package(['other.bin' => 'else', 'README.md' => 'good']);
    $check($read(substr_replace($uncorrupted, 'bad!', -8, 4)) ===
        ['path' => 'README.md', 'text' => 'good'], 'Only selected payload is hashed');
    $rejects(static fn () => $read(substr_replace($uncorrupted, 'evil', -4, 4)),
        'Selected README checksum mismatch');
    $rejects(static fn () => $read(substr($uncorrupted, 0, -1)), 'Truncated archive');
    $rejects(static fn () => $read($uncorrupted.'extra'), 'Trailing bytes');
    $rejects(static fn () => $read($package(['../README.md' => 'escape'])), 'Unsafe path');
    $rejects(static fn () => $read($package(['docs\\README.md' => 'escape'])), 'Backslash path');
    $rejects(static fn () => $read($package(['README.md' => 'x'], [
        ['path' => 'README.md', 'size' => 1, 'hash' => 'sha256:'.hash('sha256', 'x')],
        ['path' => 'README.md', 'size' => 1, 'hash' => 'sha256:'.hash('sha256', 'x')]
    ])), 'Duplicate indexed path');
    $rejects(static fn () => $read($package(['README.md' => 'x'], null, ['other.txt'])),
        'Manifest and index disagreement');
    $rejects(static fn () => $read($package(['README.md' => 'x'], [
        ['path' => 'README.md', 'size' => 2, 'hash' => 'sha256:'.hash('sha256', 'x')]
    ])), 'Invalid indexed size');
    $rejects(static fn () => $read($package(['README.md' => 'x'], [
        ['path' => 'README.md', 'size' => 1, 'hash' => 'sha256:bad']
    ])), 'Malformed indexed hash');
    $rejects(static fn () => $read('not a package'), 'Malformed archive');
    $rejects(static fn () => $read("DEMI-PROJECT-PACKAGE\n".pack('P', 2).'{}'),
        'Missing package identity');
    $rejects(static fn () => $read("DEMI-PROJECT-PACKAGE\n".pack('P', 2).'{!'),
        'Malformed header JSON');
    $rejects(static fn () => Archive::readReadme($file.'.missing'), 'Missing archive');
    echo "Archive README checks passed.\n";
} finally {
    unlink($file);
}
