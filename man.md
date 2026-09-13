# NAME

fastchess - a command-line tool for managing chess games with engines

# SYNOPSIS

fastchess [OPTIONS]

# DESCRIPTION

Fastchess is a command-line tool designed to manage and orchestrate chess games between engines. It provides a range of options to configure game settings, engine parameters, concurrency, and output formats.

# OPTIONS

The following options are available. Options are grouped by their primary function for easier navigation.
The default values are the first value in parentheses.

## General & Help

- `-version`  
    Print `fastchess` version number and exit.
- `-help`  
    Print this help message and exit.

## Tournament Setup

- `-quick cmd=ENGINE1 cmd=ENGINE2 book=BOOK`  
    Shortcut for predefined game settings involving two engines, time controls, rounds, and concurrency.

- `-concurrency (1|N)`  
    Play N games concurrently, limited by the number of hardware threads. Default value is 1.

- `-force-concurrency`  
    Ignore the hardware concurrency limit and force the specified concurrency.

- `-rounds (2|N)`  
    Play N rounds of games where each game within the round uses the same opening. Default value is 2.

- `-games (2|N)`  
    Play N games in each round. Default value is 2. Setting this higher than 2 does not provide meaningful results.

- `-repeat`  
    Set the number of games to 2. Equivalent to `-games 2`.

- `-sprt elo0=ELO0 elo1=ELO1 alpha=ALPHA beta=BETA model=(normalized|logistic|bayesian)`  
    Set parameters for a Sequential Probability Ratio Test (SPRT).

    - `model`:
        - `normalized` - Uses nElo (default).
        - `logistic` - Uses regular/logistic Elo.
        - `bayesian` - Uses BayesElo.

- `-tournament (roundrobin|gauntlet)`  
    Choose type of tournament.

    - `roundrobin` - each engine plays every other engine (default)
    - `gauntlet` - the first `-seeds` engines play against all other engines

- `-seeds (1|N)`  
    First N engines are playing the `gauntlet`. Default value is 1.

- `-variant (standard|fischerandom)`  
    Choose between the following game variants:

    - `standard` - play Standard Chess (default)
    - `fischerandom` - play Fischer Random Chess

- `-noswap`  
    Prevent swapping of colors.

- `-reverse`  
    Use a tournament schedule with reversed colors.

- `-wait (0|N)`  
    Wait N milliseconds between games. Default is 0.

## Engine Configuration

- `-each OPTIONS`  
    Apply specified OPTIONS to both engines.

- `-engine OPTIONS`  
    Apply specified OPTIONS to the next engine.

    - `cmd=COMMAND` - Specify engine command.
    - `name=NAME` - Set engine name. If not specified, the executable's filename would be used.
    - `args="ARGS"` - Pass multiple arguments using args="ARG1 ARG2".
    - `tc=moves/minutes:seconds+increment` - Time control in Cute-Chess format.
    - `timemargin=N` - Time margin for exceeding time limit.
    - `st=N` - Movetime in seconds.
    - `nodes=N` - Max number of nodes to search.
    - `restart=(off|on)` - Enable or disable engine restarts between games, default is off.
    - `plies=N` - Max number of plies (depth) to search. The alias `depth` can also be used.
    - `proto=uci` - Specify the engine protocol. Only `uci` is supported.
    - `dir=DIRECTORY` - Working directory for the engine.
    - `option.name=VALUE` - Set engine-specific options. The value for a button option should be "true" or "false".

## Adjudication & Rules

- `-draw movenumber=(0|N) movecount=(1|N) score=(0|N)`  
    Enables draw adjudication based on number of moves, move count, and score threshold.

    - `movenumber` - number of moves before checking for a draw. Default is 0.
    - `movecount` - number of consecutive moves below the score threshold. Default is 1.
    - `score` - score threshold (in centipawns) for a draw. Default is 0.

- `-resign movecount=(1|N) score=(0|N) [twosided=(false|true)]`  
    Configures engine resignation based on move count and score threshold.

    - `movecount` - number of consecutive moves above the score threshold. Default is 1.
    - `score` - score threshold (in centipawns) to resign. Default is 0.
    - `twosided` - if true, enables two-sided resignation. Defaults to false.

- `-maxmoves N`  
    Enables draw adjudication if the game reaches N moves without a result.

- `-tb PATHS`  
    Adjudicate games using Syzygy tablebases. PATHS must be a semicolon-separated (on Windows) or colon-separated (other platforms)
    list of paths to the tablebase directories.
    Only the WDL tablebase files are required.

- `-tbpieces N`  
    Only use tablebase adjudication for positions with N pieces or less.

- `-tbignore50`  
    Disable the fifty move rule for tablebase adjudication.

- `-tbadjudicate (BOTH|WIN_LOSS|DRAW)`  
    Control when tablebase adjudication is applied. SETTING can be:
    - `BOTH` - Adjudicate both wins and draws (default)
    - `WIN_LOSS` - Only adjudicate won/lost positions
    - `DRAW` - Only adjudicate drawn positions

## Opening Book Management

- `-openings file=NAME format=(epd|pgn) [order=(sequential|random)] [plies=(Max plies|N)] [start=(1|N)]`  
    Specifies an opening book file and its format for game starting positions.

    - `format` - file format, either epd or pgn.
    - `order` - order of openings (`random` or `sequential`). Default is `sequential`.
    - `plies` - number of plies for pgn. Defaults to max available plies.
    - `start` - starting index of the opening book. Default is 1.

- `-srand SEED`  
    Specify the seed for opening book randomization.

## Output & Reporting

- `-output format=(fastchess|cutechess)`  
    Choose the output format for game results (`fastchess` or `cutechess`). Default is `fastchess`.

- `-pgnout [file=NAME] [notation=(san|lan|uci)] [append=(true|false)] [nodes=(false|true)] [seldepth=(false|true)] [nps=(false|true)] [hashfull=(false|true)] [tbhits=(false|true)] [pv=(false|true)] [timeleft=(false|true)] [latency=(false|true)] [min=(false|true)] [match_line=REGEX]`
    Export games in PGN format with specified notations and optional tracking of nodes, seldepth, and others.

    - `notation`:

        - `san` - Standard Algebraic Notation (default)
        - `lan` - Long Algebraic Notation
        - `uci` - Universal Chess Interface

    - `file` - Specify filename. Default is `fastchess_(date)_(time).pgn`.
    - `append` - Append to file. Default is true.
    - `nodes` - Track node count. Default is false.
    - `seldepth` - Track seldepth. Default is false.
    - `nps` - Track nps. Default is false.
    - `hashfull` - Track hashfull. Default is false.
    - `tbhits` - Track tbhits. Default is false.
    - `pv` - Track the full pv. Default is false.
    - `timeleft` - Track time left at end of move. Default is false.
    - `latency` - Track difference between measured time and engine reported time at end of move. Default is false.
    - `min` - Minimal PGN format. Default is false.
    - `match_line` - Add lines to the PGN that match the given regex.

- `-epdout [file=NAME] [append=(true|false)]`
    Export the final position of each game in EPD format.

    - `file` - Specify filename. Default is `fastchess_(date)_(time).epd`.
    - `append` - Append to file. Default is true.

- `-csvout [file=NAME] [append=(true|false)] [interval=N] [sep=CHAR]`
    Export the running llr and elo to a CSV file, one row per interval, so that
    they can be plotted against the number of games. Purely observational, it does
    not affect the SPRT nor the scheduling. Not stored in the config file, so it
    has to be passed again when resuming with `-config`.

    Columns: `iteration`, `games`, `pairs`, `llr`, `llr_lower`, `llr_upper`, `elo`,
    `elo_err`, `elo_lower`, `elo_upper`, `nelo`, `nelo_err`, `score`, `wins`,
    `losses`, `draws` and the pentanomial counts `LL`, `LD`, `WLDD`, `WD`, `WW`.
    The llr columns are left empty without `-sprt`.

    - `file` - Specify filename. Default is `fastchess_(date)_(time).csv`.
    - `append` - Append to file. Default is false. The header is only written when
      the file is empty.
    - `interval` - Pairs (games without `-report penta`) between two rows. Default
      is the value of `-ratinginterval`.
    - `sep` - Column separator. Default is `,`.

- `-event NAME`  
    Set the event name for the PGN header.

- `-site NAME`  
    Set the site name for the PGN header.

- `-ratinginterval (10|N)`  
    Set rating interval for the report. Default is 10. Set to 0 to disable.

- `-scoreinterval (1|N)`  
    For cutechess output, set interval for printing score results. Default is 1.

- `-report penta=(true|false)`  
    Reports pentanomial statistics (for Fastchess output). Defaults to true.

## Persistence & Recovery

- `-config [file=NAME] [discard=(false|true)] [outname=(config.json|NAME)] [stats=(true|false)]`  
    Load engine configurations to resume games from previous sessions.

    - `file` - the file name to load the configuration from.
    - `discard` - discard the loaded configuration after loading. Defaults to false. This lets you specify a filename to save the
      configuration to, while ignoring it for the current session.
    - `outname` - the auto-generated file name of the config. Default is "config.json".
    - `stats` - load the stats from the config file, and ignore `append=false` for `-pgnout` and `-epdout`. Defaults to true.

- `-recover`  
    Enables crash recovery to attempt to recover the engine after a crash and continue the tournament.

- `-autosaveinterval (20|N)`  
    Automatically saves the tournament state every N games. Default is 20. Set to 0 to disable.

## Testing & Debugging

- `--compliance ENGINE_PATH [ARGS]`  
    Check the UCI compliance of an engine by running it with the specified arguments.

- `-crc32 pgn=true`  
    Calculate the CRC32 checksum for the PGN file.

- `-check-mate-pvs`
    Check that PVs for mate scores have the correct length and end in checkmate.

- `-show-latency`  
    Show the "think" latency (difference between measured and reported time) for each engine.
    Note that a lot of engines add 1ms to the time they report, so the latency will be 1ms higher than the actual latency.
    It is also possible that the latency is negative, due to different measurement methods or missing synchronization of the clocks
    between different threads.

- `-testEnv`
    Specifies that the program is running in a test environment (OpenBench/Fishtest). This will change some outputs/settings.

- `-strict`
    Makes fastchess exit and return 1 on any warning.

- `-log file=NAME level=(warn|trace|info|err|fatal) append=(true|false) compress=(false|true) realtime=(true|false) engine=(false|true)`
    Specify a log file with a specific log level.
    Set append to false to overwrite existing file. Default is true.
    Set compress to true to write to gzipped file with timestamp (ignores append). Default is false.
    By default engine logs are disabled. Set engine to true to enable them.

    - LEVEL:
        - `trace`
        - `info`
        - `warn` (default)
        - `err`
        - `fatal`

- `-use-affinity [CPUS]`  
    Enable thread affinity for binding engines to CPU cores.

    - CPUS - The cpus to use as a list of the form "3,5,7-11,13".

- `-startup-ms N`  
    Set the maximum time to wait for engine UCI initialization in milliseconds. Default is 10000.
- `-ucinewgame-ms N`  
    Set the maximum time to wait for engine UCI new game initialization in milliseconds. Default is 60000.
- `-ping-ms N`  
    Set the maximum time to wait for engine ping responses in milliseconds, this is everything else which doesn't fit the above categories. Default is 60000.


# EXAMPLES

To start a match between two engines using random openings from book.epd:

```sh
$ fastchess -engine cmd=Engine1.exe name=Engine1 -engine cmd=Engine2.exe \
  name=Engine2 -openings file=book.epd format=epd order=random \
  -each tc=60+0.6 option.Hash=64 -rounds 200 -repeat -concurrency 4
```

To start a SPRT test between two engines up to 100000 rounds (200000 games) with 3 concurrent games with one engine playing with 2 million nodes per move and 1 thread and the other playing with 1 million nodes per move and 2 threads, and both having a time control of 5 minutes plus 5 seconds increment per move, using random openings from UHO\_Lichess\_4852\_v1.epd, Fishtest adjudication rules, and saving the games to a file in a specific folder with SAN notation and nodes count:

```sh
$ fastchess \
  -engine cmd=Engine1.exe name=Engine1 option.Threads=1 nodes=2000000 \
  -engine cmd=Engine2.exe name=Engine2 option.Threads=2 nodes=1000000 \
  -openings file=UHO_Lichess_4852_v1.epd format=epd order=random \
  -each tc=300+5 -resign movecount=3 score=600 -draw movenumber=34 movecount=8 score=20 \
  -sprt elo0=0 elo1=2 alpha=0.05 beta=0.05 \
  -rounds 100000 -concurrency 3 -pgnout notation=san nodes=true file=./games/sprt.pgn
```

# AUTHORS

Fastchess was written by Disservin, Szil, PGG106, and contributors.

# REPORTING BUGS

Report any bugs to <https://github.com/Disservin/fastchess/issues>.

# COPYRIGHT

This software is licensed under the MIT license. See the LICENSE file for details.
