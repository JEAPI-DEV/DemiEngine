<?php
require dirname(__DIR__).'/vendor/autoload.php';

use App\PackageReadme;

function expectReadme(bool $condition, string $message): void
{
    if (!$condition) {
        throw new RuntimeException($message);
    }
}

$renderer = new PackageReadme();
$html = $renderer->renderMarkdown("# Installation\n\nUse **this package**.\n\n```lua\nlocal test = 1\n```\n\n| A | B |\n|---|---|\n| 1 | 2 |\n");
expectReadme(str_contains($html, '<h1>Installation</h1>'), 'heading rendered');
expectReadme(str_contains($html, '<strong>this package</strong>'), 'emphasis rendered');
expectReadme(str_contains($html, 'language-lua'), 'code fence rendered');
expectReadme(str_contains($html, '<table>'), 'table rendered');
$html = $renderer->renderMarkdown('<script>alert(1)</script><img src=x onerror=alert(1)>' . "\n\n[bad](javascript:alert%281%29) [relative](../../README.md) [web](https://example.org) ![local image](assets/test.png)");
expectReadme(!str_contains($html, '<script') && !str_contains($html, 'onerror='), 'raw HTML stripped');
expectReadme(!str_contains($html, 'javascript:') && !str_contains($html, '../../README.md'), 'unsafe and relative URLs removed');
expectReadme(str_contains($html, 'relative') && str_contains($html, 'local image'), 'link and image labels retained');
expectReadme(str_contains($html, 'href="https://example.org"'), 'web links retained');
$html = $renderer->renderMarkdown('![safe](https://example.org/image.png) ![unsafe](data:image/svg+xml,abc)');
expectReadme(str_contains($html, 'src="https://example.org/image.png"') && !str_contains($html, 'data:'), 'only HTTPS images rendered');

$file = tempnam(sys_get_temp_dir(), 'demi-readme-');
try {
    $write = static function (string $name, string $text) use ($file): void {
        $header = json_encode([
            'format_version' => 1, 'package_type' => 'DemiProjectPackage',
            'manifest' => ['format_version' => 1, 'name' => 'test.readme', 'version' => '1.0.0', 'files' => [$name]],
            'files' => [['path' => $name, 'size' => strlen($text), 'hash' => 'sha256:'.hash('sha256', $text)]],
        ]);
        file_put_contents($file, "DEMI-PROJECT-PACKAGE\n".pack('P', strlen($header)).$header.$text);
    };
    $write('README.md', '# Bundled README');
    expectReadme(str_contains($renderer->load($file) ?? '', '<h1>Bundled README</h1>'), 'loads bundled markdown');
    $write('README.txt', '<script>plain text</script>');
    expectReadme(str_contains($renderer->load($file) ?? '', '&lt;script&gt;'), 'plain text is escaped');
    $write('README.md', "\xff");
    expectReadme($renderer->load($file) === null, 'invalid UTF-8 falls back');
    $write('README.md', '   ');
    expectReadme($renderer->load($file) === null, 'empty README falls back');
    $write('source.lua', 'return {}');
    expectReadme($renderer->load($file) === null, 'no README falls back');
    file_put_contents($file, 'invalid');
    expectReadme($renderer->load($file) === null, 'corrupt archive falls back');
} finally {
    unlink($file);
}
echo "Package README rendering checks passed.\n";
