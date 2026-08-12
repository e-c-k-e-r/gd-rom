# `gd-rom`

A simple utility to generate MIL-CD `.cdi`s or GD-ROM `.gdi`s.

This handles everything from reading your `.elf` file (preferably straight from `sh-elf-gcc`/`sh-elf-g++`), stripping it, scrambling it (if outputting a MIL-CD image), creating the `IP.BIN`, and writing the final image.

## Build

To compile, simply run `make`.

## Run

To create a CDI image, run:
* `./bin/gd-rom --elf ./path/to/your/program.elf --output ./program.cdi -g "Game Name" ./path/to/your/files/`

To create a GDI, replace `cdi` with `gdi`.

Additionally:
* a raw ISO can be outputted by setting the output extension as `iso`.
* additional directories can be provided
* extensions of files to ignore can be passed
	* example: `--ignore .bsp --ignore .glb` ignores `.bsp`s and `.glb`s
* files of a given path can be ignored with `--ignore some/path/`
	* example: `--ignore shaders/ --ignore dev/` will ignore `./data/shaders/` and `./data/entity/dev/some_ent.json`

For help, run `./bin/gd-rom --help`.