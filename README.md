# RTA4
This repository contains the programs developed for evaluating the schedulability analysis algorithm RTA4, presented in the paper [*A New RM/DM Low Cost Schedulability Test*](https://ieeexplore.ieee.org/document/8115368/):

* J. M. Urriza, F. E. Páez, M. Ferrari, R. Cayssials and J. D. Orozco, "A new RM/DM low cost schedulability test," 2017 Eight Argentine Symposium and Conference on Embedded Systems (CASE), Buenos Aires, 2017, pp. 1-6. doi: [10.23919/SASE-CASE.2017.8115368](https://doi.org/10.23919/SASE-CASE.2017.8115368).

## Program description
### `wcrt-test-mbed.py`
This program perform the evaluation of multiple schedulability algorithms on a mbed board (currently a LPC1768 or a FRDM-K64F). The script automates a series of tasks:
1. Compile the `main_wcrt.cpp` program for the selected mbed board. This program measures the temporal cost of executing the selected schedulability evaluation algorithms, recording the amount of time in usecs and cpu cycles that each method requires for its execution, among other metrics.
2. Upload the new compiled binary file to the mbed board.
3. Evaluate a set of rts retrieved from the selected datastore(s) according to the required query criteria. Each one of the retrieved rts is sended to the mbed board through the serial port, and the results are readed back in the same way.
4. Save all these results as a dataframe into the specified datastore.

### `generate-xls.py`
This program generate a summary of the test reults performed with `wcrt-test-mbed.py` and save it as an Excel file.

### `wcrt-test-sim.c`
This program evaluates multiple schedulability analysis algorithms through simulations on a PC. The following libraries are required:
* [GNU Scientific Library](https://www.gnu.org/software/gsl/).
* [Libxml2](http://xmlsoft.org/) library.

To compile the program:
```
gcc -o wcrt-test-sim wcrt-test-sim.c -Wall -I/usr/include/libxml2 -L/usr/lib/i386-linux-gnu -lxml2 -lgsl -lgslcblas -lm
```

### `wcrt-test-sim.py`
Same as `wcrt-test-sim.c` but implemented in Python.

## Running a test
This section shows how to set up and use the `wcrt-test-mbed.py` program to execute a test.

### Required software
To execute the `wcrt-test-mbed.py` and running the tests, you will need:

* Python 2.7
* The GCC ARM toolchain from [ARM](https://developer.arm.com/open-source/gnu-toolchain/gnu-rm).
* If using Windows, you will also need: 
  * The `make` command tool. We have used the one provided by the [Windows Build Tools](https://gnu-mcu-eclipse.github.io/windows-build-tools/install/) of the [GNU MCU Eclipse](https://gnu-mcu-eclipse.github.io/) project.
  * The latest [mbed Windows serial port driver](https://docs.mbed.com/docs/mbed-os-handbook/en/latest/getting_started/what_need/).

Also, the following python packages are required:
* pandas
* pyocd
* mbed-ls
* bunch
* pyserial
* xlsxwriter
* tables

These packages can be easily installed using the `pip` command:
```
$ pip install pandas pyocd mbed-ls bunch pyserial xlsxwriter tables
```

### Data files
Data files with the RTS used in the tests presented in the paper could be downloaded from [http://www.rtsg.unp.edu.ar](http://www.rtsg.unp.edu.ar). The `wcrt-test-mbed.py` program retrieve the data from a Pandas DataFrame stored in a HDF5 file. Both `wcrt-test-sim.c` and `wcrt-test.sim.py` files retrive the data from XML files.

### Configuration

The configuration parameters required for running a test are stored in `json` files. The idea is that for each test should be an unique configuration file, so that any test could be easily re-executed at a later time.

#### Common configuration
The `main-config-tmpl.json` configuration file contains common configuration parameters used for running the tests. Copy the `main-config-tmpl.json` file as `main-config.json`, which will not be versioned (you can change this behaviour in the `.gitignore` file). This only need to be done once.

If the `make` command and/or the GNU ARM compiler are not present in the system or user path, open the newly created `main.json` file and set `make_path` to the path where the `Make` executable is located, and `toolchain_path` as the path to the installed ARM toolchain. Otherwise, delete these two configuration options from the file.

For example, for Windows:
```
"project": {
        "make_path": "C:\\Program Files\\GNU ARM Eclipse\\Build Tools\\2.6-201507152002\\bin\\",
        "toolchain_path": "C:\\Program Files (x86)\\GNU Tools ARM Embedded\\6 2017-q2-update\\bin\\",
        ...
```

#### Create a new test configuration
To create a new configuration file for a test, copy the file `test-config-tmpl.json` as `test1.json` (or whatever other name you like) and modify the test parameters as necessary. By default, this new configuration file will not be versioned, but you can change this behaviour in the `.gitignore` file.

### Execute the test
From a terminal or command window, and with the mbed board connected to the PC, run the `wcrt-test-mbed.py` program with the previously created configuration file:
```
python wcrt-test-mbed.py test1.json
```

### Generate an Excel report
To generate an Excel spreadsheet with a report of the results of the previous test:
```
python generate-xls.py result.h5 --key /test1 --methods RTA2 RTA3 RTA4 --metric usecs cycles
```

## Troubleshooting
* If `wcrt-test-mbed.py` fails with an error related to a missing DLL file (such as hdf5.dll or zlib.dll), try installing pytables from a [third party built wheels](http://www.pytables.org/usersguide/installation.html#id1).
* Avoid spaces in the path to the `wcrt-test-mbed.py` program.
