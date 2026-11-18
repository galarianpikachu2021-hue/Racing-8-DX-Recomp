// SPDX-License-Identifier: GPL-3.0-or-later
// Ghidra post-script: one line per function with features for cross-build matching (32-bit 3.0.3 vs 64-bit 3.0.4/4.0.1):
// entry \t size \t callees(hex, in address order of call sites) \t strings referenced (escaped) \t external symbols called \t immediates
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.*;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.data.StringDataInstance;
import java.io.*;
import java.util.*;

public class FuncFeatures extends GhidraScript {
    String str(Address a) {
        Data d = getDataAt(a);
        if (d != null && d.hasStringValue()) return String.valueOf(d.getValue());
        // undefined bytes: read a C string if it looks like text
        try {
            StringBuilder sb = new StringBuilder();
            for (int i = 0; i < 200; i++) {
                byte b = getByte(a.add(i));
                if (b == 0) break;
                if (b < 0x20 || b > 0x7e) return null;
                sb.append((char) b);
            }
            return sb.length() >= 4 ? sb.toString() : null;
        } catch (Exception e) { return null; }
    }

    @Override
    public void run() throws Exception {
        String out = getScriptArgs()[0];
        long base = currentProgram.getImageBase().getOffset();
        Memory mem = currentProgram.getMemory();
        try (PrintWriter pw = new PrintWriter(new BufferedWriter(new FileWriter(out)))) {
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                if (monitor.isCancelled()) break;
                if (f.isExternal() || f.isThunk()) continue;
                List<String> callees = new ArrayList<>(), strs = new ArrayList<>(), ext = new ArrayList<>(), imms = new ArrayList<>();
                long size = f.getBody().getNumAddresses();
                InstructionIterator ii = currentProgram.getListing().getInstructions(f.getBody(), true);
                while (ii.hasNext()) {
                    Instruction ins = ii.next();
                    for (Reference r : ins.getReferencesFrom()) {
                        Address to = r.getToAddress();
                        if (r.getReferenceType().isCall()) {
                            Function c = getFunctionAt(to);
                            if (c != null && c.isThunk()) c = c.getThunkedFunction(true);
                            if (c != null && c.isExternal()) ext.add(c.getName());
                            else if (c != null) {
                                Symbol s = c.getSymbol();
                                if (s.getSource() != SourceType.DEFAULT && !c.getName().startsWith("FUN_")) ext.add(c.getName());
                                callees.add(Long.toHexString(c.getEntryPoint().getOffset() - base));
                            } else callees.add(Long.toHexString(to.getOffset() - base));
                        } else if (r.getReferenceType().isData() && to.isMemoryAddress()) {
                            String s = str(to);
                            if (s != null) strs.add(s.replace("\t", " ").replace("\n", " ").replace("\r", " ").replace("|", "/"));
                            Symbol sy = getSymbolAt(to);
                            if (sy != null && sy.isExternal()) ext.add(sy.getName());
                        }
                    }
                    for (int op = 0; op < ins.getNumOperands(); op++)
                        for (Object o : ins.getOpObjects(op))
                            if (o instanceof Scalar) {
                                long v = ((Scalar) o).getUnsignedValue();
                                if (v >= 0x100 && v < 0x100000000L && !(v >= base && v < base + 0x2000000)) imms.add(Long.toHexString(v));
                            }
                }
                pw.println(Long.toHexString(f.getEntryPoint().getOffset() - base) + "\t" + size + "\t" + String.join(",", callees) + "\t"
                    + String.join("|", strs) + "\t" + String.join(",", ext) + "\t" + String.join(",", imms));
            }
        }
    }
}
