// SPDX-License-Identifier: GPL-3.0-or-later
// Ghidra post-script for CTGPDX.nro: lists every call to the plugin's hook/patch wrappers with constant-folded arguments.
// Args: <out file> <wrapper hex addr>=<name> ...
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.pcode.*;
import ghidra.program.model.address.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class HookSites extends GhidraScript {
    Long eval(Varnode v, int depth) throws Exception {
        if (v == null || depth > 12) return null;
        if (v.isConstant()) return v.getOffset();
        if (v.isAddress() && v.getAddress().isMemoryAddress()) {
            // a direct read of memory (e.g. literal pool)
            return (long) (getInt(v.getAddress()) & 0xffffffffL);
        }
        PcodeOp d = v.getDef();
        if (d == null) return null;
        switch (d.getOpcode()) {
            case PcodeOp.COPY: case PcodeOp.CAST: case PcodeOp.INT_ZEXT: case PcodeOp.INT_SEXT:
                return eval(d.getInput(0), depth + 1);
            case PcodeOp.INT_ADD: case PcodeOp.PTRSUB: {
                Long a = eval(d.getInput(0), depth + 1), b = eval(d.getInput(1), depth + 1);
                return (a == null || b == null) ? null : ((a + b) & 0xffffffffL);
            }
            case PcodeOp.PTRADD: {
                Long a = eval(d.getInput(0), depth + 1), b = eval(d.getInput(1), depth + 1), c = eval(d.getInput(2), depth + 1);
                return (a == null || b == null || c == null) ? null : ((a + b * c) & 0xffffffffL);
            }
            case PcodeOp.INT_SUB: {
                Long a = eval(d.getInput(0), depth + 1), b = eval(d.getInput(1), depth + 1);
                return (a == null || b == null) ? null : ((a - b) & 0xffffffffL);
            }
            case PcodeOp.LOAD: {
                Long p = eval(d.getInput(1), depth + 1);
                if (p == null) return null;
                Address a = toAddr(p);
                if (!currentProgram.getMemory().contains(a)) return null;
                return (long) (getInt(a) & 0xffffffffL);
            }
        }
        return null;
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        Map<Long, String> wrappers = new HashMap<>();
        for (int i = 1; i < args.length; i++) {
            String[] p = args[i].split("=");
            wrappers.put(Long.parseLong(p[0], 16), p[1]);
        }
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        Set<Function> callers = new TreeSet<>(Comparator.comparing(Function::getEntryPoint));
        for (long w : wrappers.keySet())
            for (Reference r : getReferencesTo(toAddr(w))) {
                Function f = getFunctionContaining(r.getFromAddress());
                if (f != null) callers.add(f);
            }
        try (PrintWriter pw = new PrintWriter(new FileWriter(args[0]))) {
            for (Function f : callers) {
                DecompileResults res = di.decompileFunction(f, 120, monitor);
                if (res == null || !res.decompileCompleted()) { pw.println("# " + f + " decompile failed"); continue; }
                HighFunction hf = res.getHighFunction();
                Iterator<PcodeOpAST> ops = hf.getPcodeOps();
                List<String> lines = new ArrayList<>();
                while (ops.hasNext()) {
                    PcodeOpAST op = ops.next();
                    if (op.getOpcode() != PcodeOp.CALL) continue;
                    long t = op.getInput(0).getAddress().getOffset();
                    if (!wrappers.containsKey(t)) continue;
                    StringBuilder sb = new StringBuilder();
                    sb.append(op.getSeqnum().getTarget()).append(" ").append(f.getName()).append(" ").append(wrappers.get(t));
                    for (int i = 1; i < op.getNumInputs() && i <= 4; i++) {
                        Long v = eval(op.getInput(i), 0);
                        sb.append(" ").append(v == null ? "?" : String.format("%x", v));
                    }
                    lines.add(sb.toString());
                }
                Collections.sort(lines);
                for (String l : lines) pw.println(l);
            }
        }
    }
}
