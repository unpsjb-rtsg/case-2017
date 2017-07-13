import os
import math
import sys
import pandas as pd
import numpy as np
from tabulate import tabulate
from argparse import ArgumentParser
import matplotlib.pyplot as plt


def test_rts(xml_file, start, count, rta_methods, args):
    import xml.etree.cElementTree as et

    context = et.iterparse(xml_file, events=('start', 'end'))
    context = iter(context)
    event, root = context.__next__()

    rts_count = 0
    rts_limit = start + (count - 1)

    rts_id, rts = 0, []
    rts_found = False

    warn_flag = False
    warn_list = []

    rta_df_results = []

    xml_fu = int(float(root.get("u")))
    xml_rts_size = int(float(root.get("n")))

    for event, elem in context:
        if elem.tag == 'S':
            if event == 'start':
                rts_id = int(float(elem.get("count")))
            if event == 'end':
                rts_found = True

        if event == 'start' and elem.tag == 'i':
            task = elem.attrib
            for k, v in task.items():
                task[k] = int(float(v))
            rts.append(task)

        if rts_found and rts:
            if rts_count >= rts_limit:
                break

            rts_count += 1

            if rts_count >= start:
                rta_results = {}
                
                # execute schedulability analysis methods and store the results
                for rta_method in rta_methods:
                    rta_results[rta_method.__name__] = rta_method(rts)                    

                # verify schedulability and wcrt
                sched_check = rta_results["rta"][0]
                wcrt_check = rta_results["rta"][1]
                for rta_method, rta_result in rta_results.items():
                    if sched_check != rta_result[0]:
                        warn_flag = True
                        warn_list.append(rts_id)
                        print("Method {0} differs: {1} vs {2}".format(rta_method, rta_result[0], sched_check))
                    if wcrt_check != rta_result[1] and rta_method != "het2":
                        warn_flag = True
                        warn_list.append(rts_id)
                        print("Some WCRT are not the same!!!")                                                            

                # add results into list
                for rta_method, rta_method_result in rta_results.items():
                    if args.task_detail:
                        for idx, metric in enumerate(["wcrt", "ceils", "loops", "for_cnt", "while_cnt"], 1):
                            result = [rta_method, rts_id, xml_fu, rta_method_result[0], metric]
                            result.extend(rta_method_result[idx])
                            rta_df_results.append(result)
                    else:
                        # only stores the total for each metric
                        result = [rta_method, rts_id, xml_fu, rta_method_result[0]]
                        # "ceils", "loops", "for_cnt", "while_cnt" -- discard "wcrt"
                        for metric_result in rta_method_result[2:]:
                            result.append(np.sum(metric_result))
                        rta_df_results.append(result)

            rts_id, rts = 0, []
            rts_found = False

        root.clear()

    del context

    if warn_flag:
        print("Errors!")
        print(warn_list)

    return [rts_count, rta_df_results]


def rta(rts):
    wcrt = [0] * len(rts)
    ceils = [0] * len(rts)
    loops = [0] * len(rts)
    for_loops = [0] * len(rts)
    while_loops = [0] * len(rts)
    schedulable = True

    t = rts[0]["C"]
    wcrt[0] = rts[0]["C"]

    for idx, task in enumerate(rts[1:], 1):
        t_mas = t + task["C"]

        loops[idx] += 1
        for_loops[idx] += 1

        while schedulable:
            t = t_mas
            w = task["C"]

            loops[idx] += 1
            while_loops[idx] += 1

            for jdx, jtask in enumerate(rts[:idx]):
                loops[idx] += 1
                for_loops[idx] += 1

                w += math.ceil(t_mas / jtask["T"]) * jtask["C"]

                ceils[idx] += 1

                if w > task["D"]:
                    schedulable = False
                    break

            t_mas = w

            if t == t_mas:
                break

        wcrt[idx] = t

        if not schedulable:
            wcrt[idx] = 0
            break

    return [schedulable, wcrt, ceils, loops, for_loops, while_loops]


def rta2(rts):
    wcrt = [0] * len(rts)
    ceils = [0] * len(rts)
    loops = [0] * len(rts)
    for_loops = [0] * len(rts)
    while_loops = [0] * len(rts)
    a = [0] * len(rts)
    schedulable = True

    for idx, task in enumerate(rts):
        a[idx] = task["C"]

    t = rts[0]["C"]
    wcrt[0] = rts[0]["C"]

    for idx, task in enumerate(rts[1:], 1):
        t_mas = t + task["C"]

        loops[idx] += 1
        for_loops[idx] += 1

        while schedulable:
            t = t_mas

            loops[idx] += 1
            while_loops[idx] += 1

            for jdx, jtask in enumerate(rts[:idx]):
                loops[idx] += 1
                for_loops[idx] += 1

                tmp = math.ceil(t_mas / jtask["T"])
                a_tmp = tmp * jtask["C"]

                t_mas += (a_tmp - a[jdx])
                ceils[idx] += 1

                if t_mas > task["D"]:
                    schedulable = False
                    break

                a[jdx] = a_tmp

            if t == t_mas:
                break

        wcrt[idx] = t

        if not schedulable:
            wcrt[idx] = 0
            break

    return [schedulable, wcrt, ceils, loops, for_loops, while_loops]


def rta3(rts):
    wcrt = [0] * len(rts)
    ceils = [0] * len(rts)
    loops = [0] * len(rts)
    for_loops = [0] * len(rts)
    while_loops = [0] * len(rts)
    a = [0] * len(rts)
    i = [0] * len(rts)
    schedulable = True
    flag = True

    for idx, task in enumerate(rts):
        a[idx] = task["C"]
        i[idx] = task["T"]

    t = rts[0]["C"]
    wcrt[0] = rts[0]["C"]

    for idx, task in enumerate(rts[1:], 1):
        t_mas = t + task["C"]

        loops[idx] += 1
        for_loops[idx] += 1

        while schedulable:
            t = t_mas

            loops[idx] += 1
            while_loops[idx] += 1

            for jdx, jtask in zip(range(len(rts[:idx]) - 1, -1, -1), reversed(rts[:idx])):
                loops[idx] += 1
                for_loops[idx] += 1
                
                if t_mas > i[jdx]:
                    tmp = math.ceil(t_mas / jtask["T"])
                    a_tmp = tmp * jtask["C"]

                    t_mas += (a_tmp - a[jdx])
                    ceils[idx] += 1
                    
                    if t_mas > task["D"]:
                        schedulable = False
                        break

                    a[jdx] = a_tmp
                    i[jdx] = tmp * jtask["T"]

            if t == t_mas:
                break

        wcrt[idx] = t

        if not schedulable:
            wcrt[idx] = 0
            break

    return [schedulable, wcrt, ceils, loops, for_loops, while_loops]


def rta4(rts):
    wcrt = [0] * len(rts)
    ceils = [0] * len(rts)
    loops = [0] * len(rts)
    for_loops = [0] * len(rts)
    while_loops = [0] * len(rts)
    a = [0] * len(rts)
    i = [0] * len(rts)
    schedulable = True

    for idx, task in enumerate(rts):
        a[idx] = task["C"]
        i[idx] = task["T"]

    t_mas = rts[0]["C"]
    wcrt[0] = rts[0]["C"]

    min_i = i[0]

    for idx, task in enumerate(rts[1:], 1):
        t_mas += task["C"]

        loops[idx] += 1
        for_loops[idx] += 1

        while t_mas > min_i:
            min_i = i[idx]

            loops[idx] += 1
            while_loops[idx] += 1


            for jdx, jtask in zip(range(len(rts[:idx]) - 1, -1, -1), reversed(rts[:idx])):
                loops[idx] += 1
                for_loops[idx] += 1

                if t_mas > i[jdx]:
                    dif_a = t_mas - a[jdx]
                    a_mas_1 = math.ceil(dif_a / (jtask["T"] - jtask["C"]))
                    ceils[idx] += 1

                    t_mas = (a_mas_1 * jtask["C"]) + dif_a

                    a[jdx] = a_mas_1 * jtask["C"]
                    i[jdx] = a_mas_1 * jtask["T"]

                    if t_mas > task["D"]:
                        schedulable = False
                        break

                if min_i > i[jdx]:
                    min_i = i[jdx]

            if not schedulable:
                break

        wcrt[idx] = t_mas

        if not schedulable:
            wcrt[idx] = 0
            break

    return [schedulable, wcrt, ceils, loops, for_loops, while_loops]


def het2(rts):
    def workload(i, b, n):
        loops[n] += 1
        while_loops[n] += 1

        f = math.floor(b / rts[i]["T"])
        c = math.ceil(b / rts[i]["T"])

        ceils[n] += 2

        branch0 = b - f * (rts[i]["T"] - rts[i]["C"])
        branch1 = c * rts[i]["C"]

        if i > 0:
            l_w = last_workload[i - 1]
            tmp = f * rts[i]["T"]
            if tmp > last_psi[i - 1]:
                l_w = workload(i - 1, tmp, n)

            branch0 += l_w
            branch1 += workload(i - 1, b, n)

        last_psi[i] = b
        last_workload[i] = branch0 if branch0 <= branch1 else branch1

        return last_workload[i]

    # metrics
    wcrt = [0] * len(rts)
    ceils = [0] * len(rts)
    loops = [0] * len(rts)
    last_psi = [0] * len(rts)
    last_workload = [0] * len(rts)
    for_loops = [0] * len(rts)
    while_loops = [0] * len(rts)
    trace_string = []
    schedulable = True

    wcrt[0] = rts[0]["C"]

    for idx, task in enumerate(rts[1:], 1):
        loops[idx] += 1
        for_loops[idx] += 1
        w = workload(idx - 1, task["D"], idx)
        if w + task["C"] > task["D"]:
            schedulable = False
            break
        wcrt[idx] = w + task["C"]

    return [schedulable, wcrt, ceils, loops, for_loops, while_loops]


def get_args():
    """ Command line arguments """
    parser = ArgumentParser(description="Evaluate schedulability tests.")
    parser.add_argument("files", help="XML file with RTS", nargs="+", type=str)
    parser.add_argument("--start", help="rts where start", type=int, default=0)
    parser.add_argument("--count", help="number of rts to test", type=int, default=1)
    parser.add_argument("--task-detail", help="stores metrics per task", 
                        default=False, action="store_true")
    parser.add_argument("--save", help="Save the results into the specified HDF5 store",
                        default=None, type=str)
    parser.add_argument("--save-key", help="Key for storing the results into a HDF5 store",
                        default=None, type=str)
    parser.add_argument("--reuse-key", help="Replace DataFrame under key in the store",
                        default=False, action="store_true")         
    parser.add_argument("--verbose", help="Show extra information.", default=False, action="store_true") 
    return parser.parse_args()


def hdf_store(args, df):
    with pd.HDFStore(args.save, complevel=9, complib='blosc') as store:
        if args.save_key in store:
            if args.reuse_key is False:
                sys.stderr.write("{0} -- key already exists (use --reuse-key).\n".format(args.save_key))
                exit(1)
            else:
                store.remove(args.save_key)

        # save the results into the store
        store.put(args.save_key, df, format='table', min_itemsize = {'values': 50})


def main():
    args = get_args()

    df_list = []
    
    # method to evaluate
    rta_methods = [het2, rta, rta2, rta3, rta4]

    for file in args.files:
        if not os.path.isfile(file):
            print("{0}: file not found.".format(file))
            sys.exit(1)

        if args.verbose:
            sys.stderr.write("Evaluating file : {0}\n".format(file))

        # evaluate the methods with the rts in file
        rts_count, df_result = test_rts(
            file, args.start, args.count, rta_methods, args)

        if args.task_detail:
            # common column names
            column_names = ["method", "rts_id", "rts_fu", "sched", "metric"]
            # add remain column names
            column_count = len(df_result[0]) - len(column_names)
            column_names.extend(["t_{0}".format(task_id) for task_id in range(column_count)])
        else:
            column_names = ["method", "rts_id", "rts_fu", "sched", "ceils", "loops", "for_cnt", "while_cnt"]

        # add DataFrame into list
        df_list.append(pd.DataFrame(df_result, columns=column_names))

    # generate a DataFrame with the results
    df = pd.concat(df_list)

    if args.save:
        hdf_store(args, df)
    

if __name__ == '__main__':
    main()
