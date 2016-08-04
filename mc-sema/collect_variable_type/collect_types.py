import idaapi

def _get_flags_from_bits(flag):
    '''
    Translates the flag field in structures (and elsewhere?) into a human readable
    string that is compatible with pasting into IDA or something.
    Returns an empty string if supplied with -1.
    '''
    if -1 == flag:
        return ""
    cls = {
      'MASK':1536,
      1536:'FF_CODE',
      1024:'FF_DATA',
      512:'FF_TAIL',
      0:'FF_UNK',
    }

    comm = {
      'MASK':1046528,
      2048:'FF_COMM',
      4096:'FF_REF',
      8192:'FF_LINE',
      16384:'FF_NAME',
      32768:'FF_LABL',
      65536:'FF_FLOW',
      524288:'FF_VAR',
      49152:'FF_ANYNAME',
    }
 
    _0type = {
      'MASK':15728640,
      1048576:'FF_0NUMH',
      2097152:'FF_0NUMD',
      3145728:'FF_0CHAR',
      4194304:'FF_0SEG',
      5242880:'FF_0OFF',
      6291456:'FF_0NUMB',
      7340032:'FF_0NUMO',
      8388608:'FF_0ENUM',
      9437184:'FF_0FOP',
      10485760:'FF_0STRO',
      11534336:'FF_0STK',
    }
    _1type = {
      'MASK':251658240,
      16777216:'FF_1NUMH',
      33554432:'FF_1NUMD',
      50331648:'FF_1CHAR',
      67108864:'FF_1SEG',
      83886080:'FF_1OFF',
      100663296:'FF_1NUMB',
      117440512:'FF_1NUMO',
      134217728:'FF_1ENUM',
      150994944:'FF_1FOP',
      167772160:'FF_1STRO',
      184549376:'FF_1STK',
    }
    datatype = {
      'MASK':4026531840,
      0:'FF_BYTE',
      268435456:'FF_WORD',
      536870912:'FF_DWRD',
      805306368:'FF_QWRD',
      1073741824:'FF_TBYT',
      1342177280:'FF_ASCI',
      1610612736:'FF_STRU',
      1879048192:'FF_OWRD',
      2147483648:'FF_FLOAT',
      2415919104:'FF_DOUBLE',
      2684354560:'FF_PACKREAL',
      2952790016:'FF_ALIGN',
    }

    output = cls[cls['MASK']&flag]
    
    for category in [comm, _0type, _1type, datatype]:
        #the ida docs define, for example, a FF_0VOID = 0 constant in with the rest
        #  of the 0type constants, but I _think_ that just means
        #  the field is unused, rather than being specific data
        val = category.get(category['MASK']&flag, None)
        if val:
            output = output + " | " + val
    return output

def _collect_func_vars():
    '''
    Collects stack variable data from all functions in the database.
    Returns a list of dictionaries with keys 'name' and 'stackArgs'.
    The 'stackArgs' value is a list of (offset, variable_name, variable_size, variable_flags) tuples.
    Skips stack arguments without names, as well as the special arguments with names " s" and " r".
    Skips functions without frames.
    variable_flags is a string with flag names.
    '''
    functions = list()
    funcs = Functions()
    for f in funcs:
        func_var_data = _collect_individual_func_vars(f)
        if func_var_data is not None:
            functions.append(func_var_data)
    return functions

def _collect_individual_func_vars(f):
    name = Name(f)
    end = GetFunctionAttr(f, FUNCATTR_END)
    _locals = GetFunctionAttr(f, FUNCATTR_FRSIZE)
    frame = GetFrame(f)
    if frame is None:
        return None
    stackArgs = list()
    offset = GetFirstMember(frame)
    #TODO: the following line should check the binary's address size as appropriate
    while offset != 0xffffffff and offset != 0xffffffffffffffff:
        memberName = GetMemberName(frame, offset)
        if memberName is None:
            #gaps in stack usage are fine, but generate trash output
            #gaps also could indicate a buffer that IDA doesn't recognize
            offset = GetStrucNextOff(frame, offset)
            continue
        if (memberName == " r" or memberName == " s"):
            #the return pointer and saved registers, skip them
            offset = GetStrucNextOff(frame, offset)
            continue
        memberSize = GetMemberSize(frame, offset)
        memberFlag = GetMemberFlag(frame, offset)
        #TODO: handle the case where a struct is encountered (FF_STRU flag)
        flag_str = _get_flags_from_bits(memberFlag)
        stackArgs.append((offset, memberName, memberSize, flag_str))
        offset = GetStrucNextOff(frame, offset)
    return {"name":name, "stackArgs":stackArgs}

def _find_local_references(func):
    #naive approach at first
    frame = GetFrame(func)
    if frame is None:
        return
    regs = dict()
    referers = set()
    for addr in FuncItems(func.startEA):
        if "lea"==GetMnem(addr):
            if "ebp" in GetOpnd(addr, 1):
                #right now just capture that it's a local reference, not its referant
                referers.add(GetOpnd(addr, 0))
        if "mov"==GetMnem(addr):
            if GetOpnd(addr, 1) in referers:
                target_op = GetOpnd(addr, 0)
                if "[ebp+" in target_op:
                    target_offset = target_op[5:target_op.index(']')]
                    #TODO: correlate the offset back with the stack vars instead of just printing it
                    print target_offset
            elif GetOpnd(addr, 0) in referers:
                referers.remove(GetOpnd(addr, 0))

_find_local_references(idaapi.get_func(here()))
entry = _collect_individual_func_vars(idaapi.get_func(here()).startEA)
print "{} {{".format(entry['name'])
for var in entry['stackArgs']:
    print "  {}".format(var)
print "}"
