<?php
namespace App;

use Symfony\Component\Filesystem\Filesystem;

final class Publisher
{
    public function __construct(private Catalog $catalog) {}

    /** imageFiles maps metadata image names to explicitly supplied local files. */
    public function publish(string $archive, array $raw, array $imageFiles = []): array
    {
        $fs = new Filesystem();
        $stage = null;
        try {
            $manifest = Archive::inspect($archive);
            if (($raw['format_version'] ?? null) !== 1) {
                throw new \InvalidArgumentException('Metadata requires format_version 1.');
            }
            $metadata = Catalog::metadata($raw);
            $destination = $this->catalog->root().'/packages/'.$manifest['name'].'/'.$manifest['version'];
            if (file_exists($destination)) {
                throw new \DomainException('This version already exists; publish a new version.');
            }
            $fs->mkdir($this->catalog->root().'/staging', 0750);
            $stage = $this->catalog->root().'/staging/'.bin2hex(random_bytes(16));
            $fs->mkdir($stage, 0750);
            $metadata['images'] = PreviewImages::store($metadata['images'], $imageFiles, $stage,
                '/media/'.$manifest['name'].'/'.$manifest['version']);
            $record = $metadata + [
                'format_version' => 1, 'manifest' => $manifest,
                'archive_hash' => 'sha256:'.hash_file('sha256', $archive),
                'bytes' => filesize($archive), 'published_at' => gmdate('Y-m-d\TH:i:s\Z'),
            ];
            $fs->copy($archive, $stage.'/package.demipkg');
            $fs->dumpFile($stage.'/release.json', json_encode($record, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES | JSON_THROW_ON_ERROR));
            $fs->mkdir(dirname($destination), 0750);
            // rename cannot replace an existing nonempty version directory.
            $fs->rename($stage, $destination, false);
            $stage = null;
            return $record;
        } finally {
            if ($stage !== null) { $fs->remove($stage); }
        }
    }
}
