<?php
namespace App\Command;

use App\Catalog;
use App\Publisher;
use Symfony\Component\Console\Attribute\AsCommand;
use Symfony\Component\Console\Command\Command;
use Symfony\Component\Console\Input\InputArgument;
use Symfony\Component\Console\Input\InputInterface;
use Symfony\Component\Console\Output\OutputInterface;

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
        try {
            $archive = realpath($input->getArgument('archive'));
            $metadataPath = realpath($input->getArgument('metadata'));
            if (!$archive || !$metadataPath) {
                throw new \InvalidArgumentException('Archive and metadata must exist.');
            }
            $raw = json_decode(file_get_contents($metadataPath), true, 512, JSON_THROW_ON_ERROR);
            $images = [];
            foreach (Catalog::metadata($raw)['images'] as $reference) {
                if (!str_starts_with($reference, 'https://')) {
                    $path = realpath(dirname($metadataPath).'/'.$reference);
                    if ($path) { $images[$reference] = $path; }
                }
            }
            $record = (new Publisher($this->catalog))->publish($archive, $raw, $images);
            $output->writeln('Published '.$record['manifest']['name'].'@'.$record['manifest']['version']);
            return Command::SUCCESS;
        } catch (\Throwable $e) {
            $output->writeln(htmlspecialchars($e->getMessage()));
            return Command::FAILURE;
        }
    }
}
