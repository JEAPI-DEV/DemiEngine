<?php
namespace App;

final class Catalog
{
    public const NAME = '/^[a-z0-9][a-z0-9._-]{0,127}$/D';
    public const VERSION = '/^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?$/D';
    public const CATEGORIES = ['Gameplay', 'Tools', 'Networking', 'UI', '3D', '2D', 'Audio', 'Templates'];

    public function root(): string
    {
        return getenv('DEMI_STORE_DATA') ?: dirname(__DIR__).'/var/catalog';
    }

    public function releases(string $name): array
    {
        if (!preg_match(self::NAME, $name) || str_contains($name, '..')) {
            return [];
        }
        $records = [];
        foreach (glob($this->root().'/packages/'.$name.'/*/release.json') ?: [] as $file) {
            $record = json_decode(file_get_contents($file), true, 512, JSON_THROW_ON_ERROR);
            // PHP decodes {} as [] in associative mode. The engine requires
            // dependency maps to remain JSON objects, including empty maps.
            $record['manifest']['dependencies'] = (object) ($record['manifest']['dependencies'] ?? []);
            $record['tags'] = $record['tags'] ?? [];
            if (!($record['yanked'] ?? false)) {
                $records[] = $record;
            }
        }
        usort($records, fn ($a, $b) => version_compare($b['manifest']['version'], $a['manifest']['version']));
        return $records;
    }

    public function all(): array
    {
        $items = [];
        foreach (glob($this->root().'/packages/*', GLOB_ONLYDIR) ?: [] as $directory) {
            $releases = $this->releases(basename($directory));
            if ($releases) {
                $items[] = $releases[0];
            }
        }
        usort($items, fn ($a, $b) => strcmp($a['title'], $b['title']));
        return $items;
    }

    public function find(string $name, ?string $version = null): ?array
    {
        foreach ($this->releases($name) as $record) {
            if ($version === null || $record['manifest']['version'] === $version) {
                return $record;
            }
        }
        return null;
    }

    public function directory(array $record): string
    {
        return $this->root().'/packages/'.$record['manifest']['name'].'/'.$record['manifest']['version'];
    }

    public static function metadata(array $input): array
    {
        foreach (['title' => 100, 'description' => 6000, 'publisher' => 100, 'license' => 120] as $key => $limit) {
            if (!is_string($input[$key] ?? null) || trim($input[$key]) === '' || strlen($input[$key]) > $limit) {
                throw new \InvalidArgumentException("$key is required (maximum $limit bytes).");
            }
        }
        if (!in_array($input['kind'] ?? null, ['code', 'asset'], true)) {
            throw new \InvalidArgumentException('kind must be code or asset.');
        }
        if (!in_array($input['category'] ?? null, self::CATEGORIES, true)) {
            throw new \InvalidArgumentException('Choose a supported category.');
        }
        if (($input['price'] ?? 0) !== 0) {
            throw new \InvalidArgumentException('Only free packages are accepted.');
        }
        if (Licenses::find($input['license']) === null) {
            throw new \InvalidArgumentException('Unknown license ID. Add it to the shared license pool first.');
        }
        $tags = $input['tags'] ?? [];
        if (!is_array($tags) || !array_is_list($tags) || count($tags) > 12) {
            throw new \InvalidArgumentException('tags must be an array of at most 12 labels.');
        }
        foreach ($tags as $tag) {
            if (!is_string($tag) || !preg_match('/^[a-z0-9][a-z0-9-]{0,39}$/D', $tag)) {
                throw new \InvalidArgumentException('Tags use lowercase letters, digits and hyphens, up to 40 characters.');
            }
        }
        $tags = array_values(array_unique($tags));
        $images = $input['images'] ?? [];
        if (!is_array($images) || !array_is_list($images) || count($images) > 8) {
            throw new \InvalidArgumentException('At most eight image paths or HTTPS image URLs are allowed.');
        }
        foreach ($images as $image) {
            if (!is_string($image) || strlen($image) > 2048 || $image === '') {
                throw new \InvalidArgumentException('Invalid image reference.');
            }
            if (str_contains($image, '://') &&
                (!filter_var($image, FILTER_VALIDATE_URL) || parse_url($image, PHP_URL_SCHEME) !== 'https' || parse_url($image, PHP_URL_USER) !== null)) {
                throw new \InvalidArgumentException('Remote image links must be HTTPS without credentials.');
            }
        }
        return array_intersect_key($input, array_flip(['title', 'description', 'publisher', 'license', 'kind', 'category'])) +
            ['price' => 0, 'images' => $images, 'tags' => $tags];
    }
}
