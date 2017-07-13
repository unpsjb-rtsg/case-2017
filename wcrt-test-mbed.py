from __future__ import print_function

import os
import sys
import serial
import xml.etree.cElementTree as et
import ntpath
import re
import struct
import glob
import datetime as dt
import pandas as pd
import mbed_lstools
import subprocess
import json
import shutil
from bunch import Bunch, unbunchify
from pyOCD.board import MbedBoard
from time import sleep
from argparse import ArgumentParser


def test_rts_in_mbed(rts, ser, methods, task_metric=None):
    # send task count
    ser.write(struct.pack('>i', len(rts)))

    # send task parameters
    for task in rts:
        ser.write(struct.pack('>i', task["C"]))
        ser.write(struct.pack('>i', task["T"]))
        ser.write(struct.pack('>i', task["D"]))

    result_str = []

    # retrieve the results
    for _ in methods:
        # read method, schedulable, usecs and cycles
        r_str = [ser.read(4) for _ in range(4)]
        if task_metric:
            if task_metric == "detail":
                for _ in rts:
                    # read wcrt, ceils, loops
                    r_str.extend([ser.read(4) for _ in range(3)])
            if task_metric == "total":
                r_str.append(ser.read(4)) # cc
                r_str.append(ser.read(4)) # loops
        result_str.append(r_str)

    magic = struct.unpack('>i', ser.read(4))[0]
    if magic != 0xABBA:
        print("Error: received wrong end code ({0})".format(magic), file=sys.stderr)
        return False, None

    # store the result data -- one list per method
    result_list = []

    for r_str in result_str:
        result = [ struct.unpack('>i', r_str[0])[0], struct.unpack('>i', r_str[1])[0],
                   struct.unpack('>i', r_str[2])[0], struct.unpack('>i', r_str[3])[0] ]

        # verify that the method id is valid
        if result[0] not in range(6):
            print("Error: invalid method id {0}".format(result[0]), file=sys.stderr)
            return False, None
        
        # verify that a valid schedulable result was sent
        if result[1] not in [0,1]:
            print("Error: invalid schedulability result {0}".format(result[1]), file=sys.stderr)
            return False, None

        if task_metric is not None:
            if task_metric == "detail":
                # wcrt, number of ceils/floors operations and amount of loops per task
                for r in r_str[4:]:
                    result.append(struct.unpack('>i', r)[0])
                    
            if task_metric == "total":
                # total ceil/floor operations and total for/while loops count
                result.append(struct.unpack('>i', r_str[4])[0])
                result.append(struct.unpack('>i', r_str[5])[0])

        result_list.append(result)

    # verify that all the methods have the same schedulability result
    sched_ref = result_list[0][1]
    for r in result_list:
        if r[1] != sched_ref:
            print("Error: {0}".format(["{0}:{1}".format(x[0], x[1]) for x in result_list]), file=sys.stderr)
            return False, None

    return True, result_list


def test_rts(rts, ser, methods, task_metric=None):
    test_ok = False

    while not test_ok:
        try:                        
            test_ok, result_t = test_rts_in_mbed(rts, ser, methods, task_metric)
        except serial.SerialTimeoutException as e:
            print("{0}: {1}".format(e.errno, e.strerror), file=sys.stderr)
        except UnicodeDecodeError as e:
            print("{0}: {1}".format(e.errno, e.strerror), file=sys.stderr)
        except struct.error as e:
            print("struct.error: {0}".format(e), file=sys.stderr)            
                    
        if not test_ok:
            # reset mbed board and wait half a second
            print("Reset", file=sys.stderr)
            ser.reset_input_buffer()
            ser.reset_output_buffer()
            ser.sendBreak(0.5)                            
            sleep(0.5)
    
    return result_t


def test_file_hdfs(g, ser, testcfg):
    results = []
    for key in g["keys"]:
        try:
            # retrieve the selected data from the store
            df = pd.read_hdf(g.file, key, where=g.where, mode='r')

            # group by rts 
            dfg = df.groupby(['tdist','trange','ntask','uf','rts_id'])
                
            print("Ready to test {0} rts from {1}...".format(len(dfg), key))
                
            for k, v in dfg:
                v.columns = [x.upper() for x in v.columns]
                rts = v.to_dict(orient='records')
                result_t = test_rts(rts, ser, testcfg.test.methods, testcfg.test.task_metric)

                for r in result_t:
                    r.extend([k[3], k[2], k[4]])
             
                results.extend(result_t)
        except KeyError as e:
            print("{0}".format(e.strerror), file=sys.stderr)
            continue

    return results


def create_df(test, results):
    # list of column names for computing sum with df.sum(axis=1)
    col_names_wcrt = []
    col_names_cc = []
    col_names_loops = []

    # column names for the data frame (the csv has no headers)
    col_names = ["method_id", "sched", "usecs", "cycles"]

    # complete columns if the csv has detailed results per task
    if test.task_metric == "detail":
        for n in range(1, test.task_count + 1):
            col_names.extend(["wcrt_{0}".format(n), "cc_{0}".format(n), "loops_{0}".format(n)])
            col_names_wcrt.append("wcrt_{0}".format(n))
            col_names_cc.append("cc_{0}".format(n))
            col_names_loops.append("loops_{0}".format(n))
    
    # complete columns i the csv has only total values for cc and # of loops
    if test.task_metric == "total":
        col_names.extend(["cc", "loops"])

    # index columns
    col_names.extend(["uf", "rts_size", "rts_id"])

    # create dataframe
    df = pd.DataFrame(results, columns=col_names)

    return df


def save_to_hdfs(args, board_info, df):    
    save = args.save
    with pd.HDFStore(save.file, complevel=9, complib='blosc') as store:
        if save.key in store.keys():
            store.remove(save.key)

        # save the results into the store
        store.put(save.key, df, format='table', min_itemsize = {'values': 50})

        # add additional metadata
        metadata = {'datetime': str(dt.datetime.now()), 'config': Bunch.toDict(args), 'board_info': board_info}
        store.get_storer(save.key).attrs.metadata = metadata

        print("Results saved in {0} store as {1}.".format(save.file, save.key))


def build_project(maincfg, testcfg):    
    make_path = "make"

    if maincfg.project.has_key("make_path"):
        make_path = os.path.join(maincfg.project.make_path, "make")

    methods_to_test = ":".join([str.upper("TEST_{0}".format(m)) for m in testcfg.test.methods])

    method_ids = ":".join([str.upper("{0}_ID={1}".format(k,v)) for k,v in maincfg.test.supported_methods.items()])
    
    make_clean = [make_path,
                  "--no-print-directory",
                  "-C", str(maincfg.project[testcfg.target.platform].path),
                  "clean"]
    
    make_call = [make_path,
                 "--no-print-directory",
                 "-C", str(maincfg.project[testcfg.target.platform].path),
                 "METHODS_TO_TEST={0}".format(methods_to_test),
                 "METHODS_IDS={0}".format(method_ids),
                 "PRINT_TASK_RESULTS={0}".format("1" if testcfg.test.task_metric else "0"),
                 maincfg.test.supported_tests[testcfg.test.test_type]]
    make_call.extend(testcfg.target.project.build_options)

    if maincfg.project.has_key("toolchain_path"):
        make_call.append("GCC_BIN='{0}'".format(maincfg.project.toolchain_path))

    print("Clean project {0}.".format(testcfg.target.platform), file=sys.stderr)
    returncode = subprocess.call(make_clean, stdout=None, stderr=None)
    
    print("Copy main_wcrt.cpp to {0} directory.".format(maincfg.project[testcfg.target.platform].path))
    try:
        shutil.copy('main_wcrt.cpp', maincfg.project[testcfg.target.platform].path)
    except (error, IOError) as e:
        print(e.strerro, file=sys.stderr)
        exit(1)
    
    print("Building project {0}.".format(testcfg.target.platform), file=sys.stderr)
    returncode = subprocess.call(make_call, stdout=None, stderr=None)

    if returncode > 0:
        exit(1)


def configure_board(maincfg, target):    
    # retrieve all connected mbed boards
    mbed_ls = mbed_lstools.create()
    connected_boards = mbed_ls.list_mbeds()

    if not connected_boards:
        print("Error: no mbed boards found.", file=sys.stderr)                
        exit(1)

    mbed_board_info = None

    if target.board.auto:
        # select the first mbed board found that match the selected platform
        for connected_board in connected_boards:
            if connected_board['platform_name'].upper() == target.platform.upper(): 
                mbed_board_info = connected_board
        
        if not mbed_board_info:
            print("Error: no {0} board found ".format(target.platform), file=sys.stderr)
            exit(1)
    else:
        # use the specified targetid to select the board
        for connected_board in connected_boards:
            if target.board.target_id == connected_board['target_id']:
                mbed_board_info = connected_board
                break
        
        if not mbed_board_info:
            print("Error: no mbed board found with target_id {0}".format(target.board.target_id), file=sys.stderr)
            exit(1)

    if target.platform.upper() != mbed_board_info['platform_name']:
        print("Error: platform mismatch ({0}, {1})".format(target.platform, mbed_board_info["platform_name"]))
        exit(1)

    # print board info
    print("Using {0} board - serial port: {1}.".format(mbed_board_info['platform_name'], mbed_board_info['serial_port']))

    # connect to the mbed board
    mbed_board = MbedBoard.chooseBoard(board_id=mbed_board_info["target_id_usb_id"], init_board=True)

    # flash the binary if specified
    if target.board.flash:
        binfile = os.path.join(maincfg.project[target.platform].path, 
                               maincfg.project[target.platform].bin_path,
                               maincfg.project.bin_file)
        try:
            print("Flash binary {0}...".format(binfile), file=sys.stderr)
            mbed_board.target.resume()
            mbed_board.target.halt()
            mbed_board.flash.flashBinary(binfile)
        except IOError as e:
            print("{0}: {1}".format(binfile, e.strerror), file=sys.stderr)            
            exit(1)
    
    # reset target board
    mbed_board.target.reset()
    mbed_board.uninit()

    return mbed_board_info


def get_args():
    """ Command line arguments """
    parser = ArgumentParser(description="Evaluate the schedulability of a set of RTS, sending them to a " + 
                                        "mbed board via serial port. The results are readed back and saved in a HDF5 " +
                                        "store, or printed into stdout by default. The test configuration " +
                                        "is loaded from a JSON file.")

    parser.add_argument("testcfg", type=str, metavar="file", help="Test configuration.")

    hdfs_group = parser.add_argument_group('HDF5 store', 'Options for saving the results into a HDF5 store.')
    hdfs_group.add_argument("--reuse-key", help="Replace the DataFrame assigned under key in the store.", default=False, action="store_true")

    return parser.parse_args()


def main():
    # get command line arguments
    args = get_args()

    # get main configuration file
    try:
        with open('main-config.json') as f:
            maincfg_dict = json.load(f)
    except IOError as err:
        print("main-config.json: {0}".format(err.strerror), file=sys.stderr)
        exit(1)    

    # get test configuration file
    try:
        with open(args.testcfg) as f:
            test_config = json.load(f)
    except IOError as err:
        print("{0}: {1}".format(args.load_config, err.strerror), file=sys.stderr)
        exit(1)

    if not "test_metric" in test_config["test"].keys():
        test_config["test"]["test_metric"] = False

    # parse the configuration files into namespaces
    testcfg = Bunch.fromDict(test_config)
    maincfg = Bunch.fromDict(maincfg_dict)

    # if using a hdfs store, check before running the tests if the specified
    # store key already exists -- if so, print an error message.
    if testcfg.has_key('save') and testcfg.save.has_key('hdfs') and not args.reuse_key:
        try:
            df = pd.read_hdf(testcfg.save.hdfs.file, testcfg.save.hdfs.key, stop=1)
            print("Error: key {0} exists (use --reuse-key).".format(testcfg.save.hdfs.key), file=sys.stderr)
            exit(1)
        except KeyError as e:            
            pass  # ok!

    # build the target project
    if testcfg.target.project.build:
        build_project(maincfg, testcfg)
    
    # select the mbed board and retrieve the serial port connection
    mbed_board_info = configure_board(maincfg, testcfg.target)

    # open the specified serial port
    ser = serial.Serial(port=mbed_board_info['serial_port'], baudrate=testcfg.target.baudrate,
                        timeout=0.5, write_timeout=0.5, xonxoff=True, dsrdtr=True)

    # dataframe list
    df_list = []

    # evaluate the given number of rts on the file(s), sending them to the 
    # mbed board, and storing the results as a pandas dataframe.
    for g in testcfg.test.data:
        try:
            df_list.append(create_df(testcfg.test, test_file_hdfs(g, ser, testcfg)))
        except IOError as e:
            print(e, file=sys.stderr)
            sys.exit(1)

    # generate a new dataframe
    df = pd.concat(df_list)

    # save the results or send them to stdout
    if testcfg.has_key('save'):
        save_to_hdfs(testcfg, mbed_board_info, df)        
    else:
        df.to_csv(path_or_buf=sys.stdout, sep=':', index=False)


if __name__ == '__main__':
    main()
