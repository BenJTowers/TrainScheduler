# MTS - Multi-threaded Train Simulation

## Description

This program simulates a multi-threaded train scheduling system where trains from east and west directions share a single track. The program reads train data from an input file, creates threads for each train, and manages the crossing of trains on the main track according to specified rules.

## Compilation

To compile the program, simply run:

make

This will produce an executable file named `mts` in the root directory.

## Usage

To run the program, use the following command:

./mts <input_file>
- `<input_file>`: A text file containing the train data. Each line in the file represents a train with the format:

<direction> <loading_time> <crossing_time>
- `<direction>`: 'E' or 'e' for Eastbound trains, 'W' or 'w' for Westbound trains.
  - Uppercase letters ('E', 'W') denote high-priority trains.
  - Lowercase letters ('e', 'w') denote low-priority trains.
- `<loading_time>`: Time in tenths of a second the train takes to load before it's ready to cross.
- `<crossing_time>`: Time in tenths of a second the train takes to cross the main track.

## Output

The program outputs the status of trains to a file named `output.txt`. The output includes timestamps and messages indicating when trains are ready, when they are on the main track, and when they have crossed.

## Functionality

- **Correct Implementation**: The program correctly implements the multi-threaded train scheduling simulation as per the specifications.
        - It does have an issue with printing trains that finish loading at the same time in a nice order but there is no specification about that.
- **Train Loading and Queuing**: Trains are loaded and queued based on their direction and priority.
- **Controller Thread**: Manages the crossing of trains on the main track, ensuring synchronization and proper scheduling according to the rules:
- No more than two trains from the same direction can cross consecutively.
- Higher-priority trains are given preference.
- If priorities are equal, trains alternate directions after two consecutive trains from the same direction.

## Implementation Details

- **Multi-threading**: Uses POSIX threads (`pthread`) for multi-threading.
- **Synchronization**: Achieved using mutexes, condition variables, and barriers.
- **Timing**: Time measurements are handled using `gettimeofday` to simulate loading and crossing times accurately.
- **Timing Variances**: The program handles potential timing variances to ensure consistent output by rounding microseconds appropriately.

## Notes

- The program assumes that `output.txt` is writable and that there are no file permission issues.
- Error messages and usage instructions are printed to `stderr`.
- All output related to the simulation is written to `output.txt`.

## Clean Up

To remove the compiled executable, run:

make clean