<?php
namespace App\Command;

use App\Catalog;
use Symfony\Component\Console\Attribute\AsCommand;
use Symfony\Component\Console\Command\Command;
use Symfony\Component\Console\Input\InputArgument;
use Symfony\Component\Console\Input\InputInterface;
use Symfony\Component\Console\Output\OutputInterface;
use App\ListingEditor;

#[AsCommand(name: 'store:update-listing', description: 'Update listing text and tags without replacing published archives or images.')]
final class UpdateListingCommand extends Command
{
    public function __construct(private Catalog $catalog) { parent::__construct(); }

    protected function configure(): void
    {
        $this->addArgument('name', InputArgument::REQUIRED)
            ->addArgument('metadata', InputArgument::REQUIRED);
    }

    protected function execute(InputInterface $input, OutputInterface $output): int
    {
        try {
            $records = $this->catalog->releases($input->getArgument('name'));
            if (!$records) { throw new \InvalidArgumentException('Package not found.'); }
            $raw = json_decode(file_get_contents($input->getArgument('metadata')), true, 512, JSON_THROW_ON_ERROR);
            if (($raw['format_version'] ?? null) !== 1) { throw new \InvalidArgumentException('format_version 1 required.'); }
            foreach ($records as $record) {
                $raw['images'] = $record['images'];
                (new ListingEditor($this->catalog))->update($record['manifest']['name'],
                    $record['manifest']['version'], $raw, [], ListingEditor::revision($record));
            }
            $output->writeln('Updated listing for '.$input->getArgument('name'));
            return Command::SUCCESS;
        } catch (\Throwable $e) {
            $output->writeln(htmlspecialchars($e->getMessage()));
            return Command::FAILURE;
        }
    }
}
