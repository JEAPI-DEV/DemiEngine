<?php
namespace App\Command;

use App\Archive;
use App\Catalog;
use Symfony\Component\Console\Attribute\AsCommand;
use Symfony\Component\Console\Command\Command;
use Symfony\Component\Console\Input\InputArgument;
use Symfony\Component\Console\Input\InputInterface;
use Symfony\Component\Console\Output\OutputInterface;
use Symfony\Component\Filesystem\Filesystem;

#[AsCommand(name: 'store:import', description: 'Validate and publish an immutable free package from local files.')]
final class ImportCommand extends Command
{
    public function __construct(private Catalog $catalog) { parent::__construct(); }

    protected function configure(): void
    {
        $this->addArgument('archive', InputArgument::REQUIRED, 'Path to a .demipkg archive')
            ->addArgument('metadata', InputArgument::REQUIRED, 'Path to catalog metadata JSON');
    }

    protected function execute(InputInterface $input, OutputInterface $output): int
    {
        $fs = new Filesystem();
        $stage = null;
        try {
            $archive = realpath($input->getArgument('archive'));
            $metadataPath = realpath($input->getArgument('metadata'));
            if (!$archive || !$metadataPath) {
                throw new \InvalidArgumentException('Archive and metadata must exist.');
            }
            $manifest = Archive::inspect($archive);
            $raw = json_decode(file_get_contents($metadataPath), true, 512, JSON_THROW_ON_ERROR);
            if (($raw['format_version'] ?? null) !== 1) {
                throw new \InvalidArgumentException('Metadata requires format_version 1.');
            }
            $metadata = Catalog::metadata($raw);
            $destination = $this->catalog->root().'/packages/'.$manifest['name'].'/'.$manifest['version'];
            if (file_exists($destination)) {
                throw new \InvalidArgumentException('This version already exists; publish a new version.');
            }
            $fs->mkdir($this->catalog->root().'/staging', 0750);
            $stage = $this->catalog->root().'/staging/'.bin2hex(random_bytes(16));
            $fs->mkdir($stage, 0750);
            $images = [];
            foreach ($metadata['images'] as $index => $reference) {
                if (str_starts_with($reference, 'https://')) {
                    $images[] = $reference;
                    continue;
                }
                $file = realpath(dirname($metadataPath).'/'.$reference);
                $image = $file ? @getimagesize($file) : false;
                $extension = $image ? match ($image['mime']) {
                    'image/png' => 'png', 'image/jpeg' => 'jpg', 'image/webp' => 'webp', default => null
                } : null;
                if (!$file || !$extension || filesize($file) > 10*1024*1024 || $image[0]*$image[1] > 40000000) {
                    throw new \InvalidArgumentException('Images must be PNG/JPEG/WebP, at most 10 MB and 40 megapixels.');
                }
                $filename = 'image-'.$index.'.'.$extension;
                $fs->copy($file, $stage.'/'.$filename);
                $images[] = '/media/'.$manifest['name'].'/'.$manifest['version'].'/'.$filename;
            }
            $metadata['images'] = $images ?: ['/images/demi-asset.svg'];
            $record = $metadata + [
                'format_version' => 1, 'manifest' => $manifest,
                'archive_hash' => 'sha256:'.hash_file('sha256', $archive),
                'bytes' => filesize($archive), 'published_at' => gmdate('Y-m-d\TH:i:s\Z'),
            ];
            $fs->copy($archive, $stage.'/package.demipkg');
            $fs->dumpFile($stage.'/release.json', json_encode($record, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES | JSON_THROW_ON_ERROR));
            $fs->mkdir(dirname($destination), 0750);
            $fs->rename($stage, $destination, false);
            $stage = null;
            $output->writeln('<info>Published '.$manifest['name'].'@'.$manifest['version'].'</info>');
            return Command::SUCCESS;
        } catch (\Throwable $e) {
            if ($stage !== null) { $fs->remove($stage); }
            $output->writeln('<error>'.htmlspecialchars($e->getMessage()).'</error>');
            return Command::FAILURE;
        }
    }
}

