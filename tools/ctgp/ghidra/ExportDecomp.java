// SPDX-License-Identifier: GPL-3.0-or-later
// Ghidra headless post-script: writes every function's decompiled C to <out>.c and the symbol list to <out>.syms
// Usage: analyzeHeadless <proj dir> <proj> -import <elf> -postScript ExportDecomp.java <out prefix> [only:<hex start>-<hex end>,...]
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.*;
import java.io.*;
import java.util.*;

public class ExportDecomp extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String out = args[0];
        List<long[]> ranges = new ArrayList<>();
        if (args.length > 1 && args[1].startsWith("only:"))
            for (String r : args[1].substring(5).split(",")) {
                String[] p = r.split("-");
                ranges.add(new long[]{Long.parseUnsignedLong(p[0], 16), Long.parseUnsignedLong(p[1], 16)});
            }
        try (PrintWriter syms = new PrintWriter(new FileWriter(out + ".syms"))) {
            SymbolIterator it = currentProgram.getSymbolTable().getAllSymbols(true);
            while (it.hasNext()) {
                Symbol s = it.next();
                if (s.getSource() == SourceType.DEFAULT) continue;
                syms.println(s.getAddress() + " " + s.getSymbolType() + " " + s.getName(true));
            }
        }
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        long imageBase = currentProgram.getImageBase().getOffset();
        try (PrintWriter pw = new PrintWriter(new FileWriter(out + ".c"))) {
            FunctionIterator fit = currentProgram.getFunctionManager().getFunctions(true);
            int n = 0;
            while (fit.hasNext() && !monitor.isCancelled()) {
                Function f = fit.next();
                long a = f.getEntryPoint().getOffset() - imageBase;
                if (!ranges.isEmpty()) {
                    boolean in = false;
                    for (long[] r : ranges) if (a >= r[0] && a < r[1]) in = true;
                    if (!in) continue;
                }
                DecompileResults res = di.decompileFunction(f, 60, monitor);
                pw.println("// ==== " + f.getName() + " @ " + f.getEntryPoint() + " (+" + Long.toHexString(a) + ")");
                if (res != null && res.decompileCompleted()) pw.println(res.getDecompiledFunction().getC());
                else pw.println("// decompile failed");
                n++;
            }
            println("decompiled " + n + " functions");
        }
    }
}
