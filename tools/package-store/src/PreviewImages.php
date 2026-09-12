<?php
namespace App;

use Symfony\Component\Filesystem\Filesystem;

final class PreviewImages
{
    /** Existing references may be retained; other local references must be supplied uploads. */
    public static function store(array $references, array $files, string $directory, string $url, array $existing = []): array
    {
        $images = [];
        foreach ($references as $reference) {
            if (str_starts_with($reference, 'https://') || in_array($reference, $existing, true)) {
                $images[] = $reference;
                continue;
            }
            $file = $files[$reference] ?? null;
            $image = $file && is_file($file) ? @getimagesize($file) : false;
            $extension = $image ? match ($image['mime']) {
                'image/png' => 'png', 'image/jpeg' => 'jpg', 'image/webp' => 'webp', default => null
            } : null;
            if (!$file || !$extension || filesize($file) > 10*1024*1024 || $image[0]*$image[1] > 40000000) {
                throw new \InvalidArgumentException('Supply valid PNG/JPEG/WebP previews, at most 10 MB and 40 megapixels each.');
            }
            // Content-addressed images never overwrite a preview used by an older page.
            $filename = 'image-'.hash_file('sha256', $file).'.'.$extension;
            (new Filesystem())->copy($file, $directory.'/'.$filename);
            $images[] = $url.'/'.$filename;
        }
        return $images ?: ['/images/demi-asset.svg'];
    }
}
