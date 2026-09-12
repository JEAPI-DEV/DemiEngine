<?php
namespace App;

use Symfony\Component\Filesystem\Filesystem;
use Symfony\Component\HttpKernel\Exception\NotFoundHttpException;

final class ListingEditor
{
    public function __construct(private Catalog $catalog) {}

    public static function revision(array $record): string
    {
        return hash('sha256', json_encode($record, JSON_THROW_ON_ERROR));
    }

    public function update(string $name, string $version, array $raw, array $images, string $revision): array
    {
        $record = $this->catalog->find($name, $version);
        if (!$record) { throw new NotFoundHttpException('Package version not found.'); }
        if (($raw['format_version'] ?? null) !== 1) {
            throw new \InvalidArgumentException('Metadata requires format_version 1.');
        }
        $allowed = ['format_version','title','description','publisher','license','kind','category','price','tags','images'];
        if (array_diff(array_keys($raw), $allowed)) {
            throw new \InvalidArgumentException('Only listing details can be edited, not release identity or archive data.');
        }
        $metadata = Catalog::metadata($raw);
        $directory = $this->catalog->directory($record);
        $lock = fopen($directory.'/.listing.lock', 'c');
        if (!$lock) { throw new \RuntimeException('Cannot lock listing.'); }
        try {
            if (!flock($lock, LOCK_EX)) { throw new \RuntimeException('Cannot lock listing.'); }
            $record = $this->catalog->find($name, $version);
            if (!hash_equals(self::revision($record), $revision)) {
                throw new \DomainException('This listing changed. Reload it before saving your edits.');
            }
            $metadata['images'] = PreviewImages::store($metadata['images'], $images, $directory,
                '/media/'.$name.'/'.$version, $record['images']);
            $updated = array_replace($record, $metadata);
            (new Filesystem())->dumpFile($directory.'/release.json',
                json_encode($updated, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES | JSON_THROW_ON_ERROR));
            return $updated;
        } finally {
            flock($lock, LOCK_UN);
            fclose($lock);
        }
    }
}
