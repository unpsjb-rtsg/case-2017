from __future__ import print_function

import os
import re
import sys
import pandas as pd
import numpy as np
from argparse import ArgumentParser


def draw_graph(xls_writer, sheet, data, row_start):
    # Access the XlsxWriter workbook and worksheet objects from the dataframe.
    workbook = xls_writer.book
    worksheet = xls_writer.sheets[sheet]

    method_count = len(data.index.get_level_values(0).unique())
    fu_count = len(data.index.get_level_values(1).unique())
    fu_max = max(data.index.get_level_values(1))
    fu_min = min(data.index.get_level_values(1))

    metrics = data.columns.levels[0]  # ceils, loops, ...
    funcs = data.columns.levels[1]    # mean, amax, ...

    from xlsxwriter.utility import xl_rowcol_to_cell

    # Generate charts for metric/func
    for i, metric in enumerate(metrics, 0):
        col_start = (len(funcs) * i) + 2  # +2 for method and uf columns
        col_graph = (8 * i) + (len(metrics) * len(funcs)) + 2 + 1  # +2 for method and uf columns, +1 to give an extra column separation
        row_graph = 3
    
        for j, func in enumerate(funcs, 0):
            col_values = col_start + j
            row_start_tmp = row_start

            # Create charts objects
            chart = workbook.add_chart({'type': 'scatter'})

            # Add data series into the charts, one per method
            for i, method in enumerate(data.index.get_level_values(0).unique(), 0):
                
                # do not plot this metrics for HET2
                if method == "HET2" and metric in ['loops', 'for_cnt', 'while_cnt']:
                    row_start_tmp += fu_count
                    continue
                
                serie_data = {
                    'name': [sheet, row_start_tmp, 0],
                    'categories': [sheet, row_start_tmp, 1, row_start_tmp + (fu_count - 1), 1],
                    'values': [sheet, row_start_tmp, col_values, row_start_tmp + (fu_count - 1), col_values],
                    'marker': {'type': 'automatic'},
                    'line': {'dash_type': 'solid'}
                }            

                chart.add_series(serie_data)
                row_start_tmp += fu_count

            # Configure the charts axes
            chart.set_x_axis({'min': fu_min, 'max': fu_max})
        
            # Insert the charts into the worksheet            
            worksheet.insert_chart(xl_rowcol_to_cell(row_graph, col_graph), chart)

            row_graph += fu_count


def add_metadata(xls_writer, metadata):
    workbook  = xls_writer.book
    worksheet = workbook.add_worksheet('metadata')

    string_format = workbook.add_format({'bold': True})
    date_format = workbook.add_format({'num_format': 'dd/mm/yy hh:mm'})

    d = dict()
    d["datetime"] = metadata["datetime"]
    d["board"] = metadata["board_info"]["platform_name"]
    d["file"] = metadata["store"]
    d["key"] = metadata["store_key"]

    row = 1
    col = 0
    for k, v in d.items():
        worksheet.write_string(row, col, k, string_format)
        worksheet.write(row, col + 1, v)
        row += 1


def generate_xls(df, xls_file, methods=[], metrics=[], metadata=None, overwrite=False):
    if os.path.isfile(xls_file) and not overwrite:
        print("Error: file {0} already exists.".format(xls_file), file=sys.stderr)
        exit(1)

    # uppercase the method names specified by the user
    methods = [m.upper() for m in methods]
    
    # select only the specified columns that exists in the dataframe
    cols = [col for col in metrics if col in df.columns]

    if not cols:
        print("Warning: the specified columns do not exists in the dataframe.", file=sys.stderr)
        cols = list(df.columns)

    # rename 'fu' column if exists
    df = df.rename(columns = {'fu':'uf'})

    if 'method' in list(df.columns):
        # if present, replace sjodin with rta, and rta7 with rta4
        df.loc[df['method'] == 'sjodin', 'method'] = 'rta'
        df.loc[df['method'] == 'rta7', 'method'] = 'rta4'
        # capitalized method names
        df['method'] = df['method'].str.upper()
    else:
        method_names = {0: 'het', 1: 'het2', 2: 'rta', 3: 'rta2', 4: 'rta3', 5: 'rta4'}
        df['method'] = df['method_id'].apply(lambda x: method_names[x].upper())

    # select only the rows with the specified methods
    if methods:
        df = df[df['method'].isin(methods)]
        if df.empty:
            print("Error: no valid methods found.", file=sys.stderr)
            exit(1)

    # compute mean and other metrics.
    df_method_rts_agg = df.groupby(["method", "uf"], as_index=False)[cols].agg([np.mean, np.max, np.min, np.median, np.std])

    # create a Pandas Excel writer using XlsxWriter as the engine.
    writer = pd.ExcelWriter(xls_file, engine='xlsxwriter')

    df_method_rts_agg.to_excel(writer, sheet_name="All")
    draw_graph(writer, 'All', df_method_rts_agg, 3)

    # save metadata
    if metadata:
        add_metadata(writer, metadata)

    # save the data and close the Pandas Excel writer and output the Excel file.
    writer.save()
    writer.close()


def get_valid_filename(s):
    """
    Return the given string converted to a string that can be used for a clean
    filename. Remove leading and trailing spaces; convert other spaces to
    underscores; and remove anything that is not an alphanumeric, dash,
    underscore, or dot.
    >>> get_valid_filename("john's portrait in 2004.jpg")
    'johns_portrait_in_2004.jpg'
    (from https://github.com/django/django/blob/master/django/utils/text.py)
    """
    s = str(s).strip().replace(' ', '_')
    s = s.replace('/', '_')
    return re.sub(r'(?u)[^-\w.]', '', s)


def get_args():
    """ Command line arguments """
    default_methods = ["het2", "rta", "rta2", "rta3", "rta4"]

    parser = ArgumentParser(description="Retrieve the selected dataframe(s) and generate an Excel file with the specified columns.")
    parser.add_argument("file", help="Result file.", type=str, default=None)
    parser.add_argument("--keys", help="Key(s) used to retrieve DataFrame from the store.", nargs="+", type=str)
    parser.add_argument("--list-keys", help="Print stored dataframes.", default=False, action="store_true")
    parser.add_argument("--list-cols", help="Print the column names of the stored dataframe(s).", default=False, action="store_true")
    parser.add_argument("--cols", help="Data columns to be used.", nargs="+", default=["usecs", "cycles"])
    parser.add_argument("--methods", help="Methods to include in the report.", nargs="+")
    parser.add_argument("--overwrite", help="Overwrite XLS file if exists.", default=False, action="store_true")
    parser.add_argument("--save-as", help="Name for the output xls file.", type=str, metavar="file")
    return parser.parse_args()


def hdfs_file(args):
    # list stored dataframes
    if args.list_keys:
        # open to check if file exists
        with open(args.file, 'r') as f:
            with pd.HDFStore(args.file) as store:
                for key in store.keys():
                    print(key, ": ", store[key].shape)
        exit(0)

    # print columns of selected dataframes
    if args.list_cols:
        for key in args.keys:
            try:
                df = pd.read_hdf(args.file, key)
                print(key, ": ", list(df.columns))
            except KeyError as e:
                print(e)        
        exit(0)

    # generate xls files
    for key in args.keys:
        try:
            df = pd.read_hdf(args.file, key)

            with pd.HDFStore(args.file) as store:
                metadata = store.get_storer(key).attrs.metadata
                if metadata:
                    metadata["store"] = args.file
                    metadata["store_key"] = key
            
            xls = get_valid_filename(args.save_as if args.save_as else "{0}.xlsx".format(key))
            
            generate_xls(df, xls, args.methods, args.cols, metadata, args.overwrite)
        except KeyError as e:
            print(e)       

def main():
    args = get_args()

    try:
        hdfs_file(args)        
    except IOError as e:
        print(e)


if __name__ == '__main__':
    main()
