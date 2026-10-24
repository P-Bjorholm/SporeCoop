// Batch queries against a SporeCoop Ghidra project (headless post-script, run by tools/ghidra/q.sh).
//
//   -postScript Q.java <queries-file> <output-file>
//
// One query per line, '#' starts a comment. <a> is a hex address (0x00C3CEA0 or C3CEA0), a symbol
// name from the build's .sym files (Simulator::cGameDataUFO::TakeDamage), or either plus an offset
// (Name+0x1C). Queries:
//   dec <a>            decompile the function containing <a> (creates one if none, not saved)
//   asm <a> [n]        n instructions from <a> (default 40), disassembling on the fly if needed
//   fasm <a>           the whole function containing <a>, in address order
//   info <a>           function / symbol / block at <a>: entry, size, signature, ret N, comments
//   xref <a>           references to <a> (code and data), with the function they are in
//   callers <a>        functions calling the function containing <a>, with the call sites
//   callees <a>        functions called from the function containing <a>
//   calls <a>          raw E8/E9 rel32 sites anywhere in .text that target <a>, seen by Ghidra or not
//   scan <hex-dword>   every aligned or unaligned 32-bit little-endian occurrence in initialized memory
//   bytes <pattern>    byte pattern search, e.g. "8B 0D ?? ?? ?? ?? 85 C9" (?? = any byte)
//   str <text>         ASCII and UTF-16LE occurrences of <text> (case sensitive) and references to them
//   dd <a> [n]         n dwords from <a> (default 16) with what each points to (vtables, tables)
//   sym <substring>    symbols whose qualified name contains <substring> (case insensitive)
// Output: every query starts with "=== <query line>"; long outputs are capped (see LIMIT).
//
// @category SporeCoop
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.app.util.NamespaceUtils;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.CommentType;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;

import java.io.BufferedReader;
import java.io.FileReader;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

public class Q extends GhidraScript {

    private static final int LIMIT = 400;
    private PrintWriter out;
    private DecompInterface decomp;
    private MemoryBlock text;
    private byte[] textBytes;

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) throw new Exception("usage: Q.java <queries> <output>");
        out = new PrintWriter(args[1], StandardCharsets.UTF_8);
        text = currentProgram.getMemory().getBlock(".text");
        long t0 = System.currentTimeMillis();
        int n = 0;
        try (BufferedReader r = new BufferedReader(new FileReader(args[0]))) {
            String line;
            while ((line = r.readLine()) != null) {
                line = line.trim();
                if (line.isEmpty() || line.startsWith("#")) continue;
                out.println("=== " + line);
                long tq = System.currentTimeMillis();
                try {
                    query(line);
                } catch (Exception e) {
                    out.println("ERROR: " + e);
                }
                out.println("# " + (System.currentTimeMillis() - tq) + " ms");
                out.println();
                out.flush();
                n++;
            }
        } finally {
            if (decomp != null) decomp.dispose();
            out.println("# " + n + " queries in " + (System.currentTimeMillis() - t0) + " ms ("
                + currentProgram.getName() + ")");
            out.close();
        }
    }

    private void query(String line) throws Exception {
        String[] p = line.split("\\s+", 2);
        String cmd = p[0].toLowerCase();
        String arg = p.length > 1 ? p[1].trim() : "";
        switch (cmd) {
            case "dec": dec(addr(arg)); break;
            case "asm": {
                String[] q = arg.split("\\s+");
                asm(addr(q[0]), q.length > 1 ? Integer.decode(q[1]) : 40);
                break;
            }
            case "fasm": fasm(addr(arg)); break;
            case "info": info(addr(arg)); break;
            case "xref": xref(addr(arg)); break;
            case "callers": callers(addr(arg)); break;
            case "callees": callees(addr(arg)); break;
            case "calls": calls(addr(arg)); break;
            case "scan": scan(arg); break;
            case "bytes": bytes(arg); break;
            case "str": str(arg); break;
            case "dd": {
                String[] q = arg.split("\\s+");
                dd(addr(q[0]), q.length > 1 ? Integer.decode(q[1]) : 16);
                break;
            }
            case "sym": sym(arg); break;
            default: out.println("ERROR: unknown query '" + cmd + "'");
        }
    }

    // ---- helpers ----------------------------------------------------------------------------

    private Address addr(String s) throws Exception {
        s = s.trim();
        if (s.isEmpty()) throw new Exception("missing address");
        long off = 0;
        int plus = s.lastIndexOf('+');
        if (plus > 0) {
            off = Long.decode(s.substring(plus + 1).trim());
            s = s.substring(0, plus).trim();
        }
        String h = s.startsWith("0x") || s.startsWith("0X") ? s.substring(2) : s;
        if (h.matches("[0-9a-fA-F]{5,8}")) return toAddr(Long.parseLong(h, 16) + off);
        List<Symbol> syms = NamespaceUtils.getSymbols(s, currentProgram);
        if (syms.isEmpty()) {
            SymbolIterator it = currentProgram.getSymbolTable().getSymbols(s);
            while (it.hasNext()) syms.add(it.next());
        }
        if (syms.isEmpty()) throw new Exception("unknown symbol '" + s + "'");
        if (syms.size() > 1) {
            StringBuilder b = new StringBuilder();
            for (Symbol y : syms) b.append(" ").append(y.getAddress());
            out.println("# '" + s + "' is ambiguous, using the first of:" + b);
        }
        return syms.get(0).getAddress().add(off);
    }

    private String name(Address a) {
        Symbol s = currentProgram.getSymbolTable().getPrimarySymbol(a);
        return s == null ? null : s.getName(true);
    }

    // "0x00C3CEA0 Simulator::cGameDataUFO::TakeDamage+0x1C"
    private String where(Address a) {
        StringBuilder b = new StringBuilder("0x").append(String.format("%08X", a.getOffset()));
        Function f = getFunctionContaining(a);
        if (f != null) {
            long d = a.subtract(f.getEntryPoint());
            b.append(" ").append(f.getName(true));
            if (d != 0) b.append(String.format("+0x%X", d));
        } else {
            String n = name(a);
            if (n != null) b.append(" ").append(n);
            else {
                MemoryBlock blk = currentProgram.getMemory().getBlock(a);
                if (blk != null) b.append(" [").append(blk.getName()).append("]");
            }
        }
        return b.toString();
    }

    private Function functionFor(Address a, boolean create) {
        Function f = getFunctionContaining(a);
        if (f != null || !create) return f;
        if (currentProgram.getListing().getInstructionAt(a) == null) {
            new DisassembleCommand(a, null, true).applyTo(currentProgram, monitor);
        }
        CreateFunctionCmd cf = new CreateFunctionCmd(a);
        if (cf.applyTo(currentProgram, monitor)) {
            out.println("# no function contained " + a + ": created one there (not saved)");
            return getFunctionAt(a);
        }
        return null;
    }

    private String sig(Function f) {
        return f.getPrototypeString(false, false) + "  [" + f.getCallingConventionName()
            + ", stack purge " + f.getStackPurgeSize() + ", " + f.getBody().getNumAddresses()
            + " bytes]";
    }

    private void comments(Address a) {
        String pc = currentProgram.getListing().getComment(CommentType.PLATE, a);
        if (pc != null) for (String l : pc.split("\n")) out.println("// " + l);
    }

    // ---- queries ----------------------------------------------------------------------------

    private void dec(Address a) throws Exception {
        Function f = functionFor(a, true);
        if (f == null) {
            out.println("ERROR: no function at " + a);
            return;
        }
        if (decomp == null) {
            decomp = new DecompInterface();
            DecompileOptions o = new DecompileOptions();
            o.grabFromProgram(currentProgram);
            o.setNamespaceStrategy(DecompileOptions.NamespaceStrategy.All);
            o.setEOLCommentIncluded(true);
            o.setPLATECommentIncluded(false);
            decomp.setOptions(o);
            decomp.toggleCCode(true);
            decomp.openProgram(currentProgram);
        }
        out.println("// " + where(f.getEntryPoint()) + "  " + sig(f));
        comments(f.getEntryPoint());
        DecompileResults r = decomp.decompileFunction(f, 90, monitor);
        if (r == null || !r.decompileCompleted()) {
            out.println("ERROR: decompile failed: " + (r == null ? "null" : r.getErrorMessage()));
            return;
        }
        out.println(r.getDecompiledFunction().getC());
    }

    private void printInsn(Instruction ins) {
        Address a = ins.getAddress();
        String lbl = name(a);
        if (lbl != null && getFunctionAt(a) == null && !lbl.startsWith("LAB_")) out.println(lbl + ":");
        StringBuilder bytes = new StringBuilder();
        try {
            for (byte x : ins.getBytes()) bytes.append(String.format("%02X", x & 0xFF));
        } catch (Exception e) {
            bytes.append("??");
        }
        StringBuilder b = new StringBuilder(String.format("%08X  %-16s %s", a.getOffset(),
            bytes, ins.toString()));
        for (Address t : ins.getFlows()) {
            b.append("   ; -> ").append(where(t));
        }
        for (Reference ref : ins.getReferencesFrom()) {
            if (ref.getReferenceType().isFlow()) continue;
            Address t = ref.getToAddress();
            if (!t.isMemoryAddress()) continue;
            String n = name(t);
            if (n != null) b.append("   ; ").append(n);
        }
        String eol = currentProgram.getListing().getComment(CommentType.EOL, a);
        if (eol != null) b.append("   // ").append(eol.replace('\n', ' '));
        out.println(b);
    }

    private void asm(Address a, int n) {
        Listing l = currentProgram.getListing();
        Instruction ins = l.getInstructionAt(a);
        if (ins == null) {
            new DisassembleCommand(a, null, false).applyTo(currentProgram, monitor);
            ins = l.getInstructionAt(a);
        }
        Function f = getFunctionAt(a);
        if (f != null) {
            out.println("// " + where(a) + "  " + sig(f));
            comments(a);
        }
        for (int i = 0; i < n && ins != null; i++) {
            printInsn(ins);
            Address next = ins.getMaxAddress().next();
            if (next == null) break;
            Instruction nx = l.getInstructionAt(next);
            if (nx == null) {
                new DisassembleCommand(next, null, false).applyTo(currentProgram, monitor);
                nx = l.getInstructionAt(next);
            }
            ins = nx;
        }
    }

    private void fasm(Address a) {
        Function f = functionFor(a, true);
        if (f == null) {
            out.println("ERROR: no function at " + a);
            return;
        }
        out.println("// " + where(f.getEntryPoint()) + "  " + sig(f));
        comments(f.getEntryPoint());
        InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
        int n = 0;
        while (it.hasNext()) {
            if (++n > 4 * LIMIT) {
                out.println("... (cut at " + 4 * LIMIT + " instructions)");
                break;
            }
            printInsn(it.next());
        }
    }

    private void info(Address a) {
        out.println("address  " + where(a));
        MemoryBlock blk = currentProgram.getMemory().getBlock(a);
        out.println("block    " + (blk == null ? "-" : blk.getName()));
        for (Symbol s : currentProgram.getSymbolTable().getSymbols(a)) {
            out.println("symbol   " + s.getName(true) + (s.isPrimary() ? " (primary)" : ""));
        }
        Function f = getFunctionContaining(a);
        if (f != null) {
            out.println("function " + f.getName(true) + " @ " + f.getEntryPoint());
            out.println("         " + sig(f));
            out.println("         body " + f.getBody().getMinAddress() + ".." + f.getBody().getMaxAddress());
            comments(f.getEntryPoint());
        }
        Data d = currentProgram.getListing().getDefinedDataContaining(a);
        if (d != null) out.println("data     " + d.getDataType().getName() + " @ " + d.getAddress()
            + " = " + d.getDefaultValueRepresentation());
        if (f == null || !f.getEntryPoint().equals(a)) comments(a);
    }

    private void xref(Address a) {
        int n = 0;
        ReferenceIterator it = currentProgram.getReferenceManager().getReferencesTo(a);
        while (it.hasNext()) {
            Reference r = it.next();
            if (++n > LIMIT) {
                out.println("... (more than " + LIMIT + ")");
                break;
            }
            out.println(String.format("%-10s from %s", r.getReferenceType(), where(r.getFromAddress())));
        }
        if (n == 0) out.println("(no references; try 'calls' or 'scan' for code Ghidra did not see)");
    }

    private void callers(Address a) {
        Function f = getFunctionContaining(a);
        Address entry = f != null ? f.getEntryPoint() : a;
        out.println("// callers of " + where(entry));
        Map<String, List<String>> by = new TreeMap<>();
        ReferenceIterator it = currentProgram.getReferenceManager().getReferencesTo(entry);
        while (it.hasNext()) {
            Reference r = it.next();
            Function c = getFunctionContaining(r.getFromAddress());
            String key = c == null ? "(data or no function)" : String.format("0x%08X %s",
                c.getEntryPoint().getOffset(), c.getName(true));
            by.computeIfAbsent(key, k -> new ArrayList<>()).add(
                r.getReferenceType() + "@" + r.getFromAddress());
        }
        int n = 0;
        for (Map.Entry<String, List<String>> e : by.entrySet()) {
            if (++n > LIMIT) {
                out.println("... (more than " + LIMIT + ")");
                break;
            }
            out.println(e.getKey() + "   " + String.join(" ", e.getValue()));
        }
        if (by.isEmpty()) out.println("(none; try 'calls' or 'scan')");
    }

    private void callees(Address a) {
        Function f = functionFor(a, true);
        if (f == null) {
            out.println("ERROR: no function at " + a);
            return;
        }
        out.println("// callees of " + where(f.getEntryPoint()));
        Map<Long, List<String>> by = new TreeMap<>();
        InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
        while (it.hasNext()) {
            Instruction ins = it.next();
            if (!ins.getFlowType().isCall()) continue;
            Address[] fl = ins.getFlows();
            String site = ins.getAddress().toString();
            if (fl.length == 0) {
                by.computeIfAbsent(-1L, k -> new ArrayList<>()).add(site + " " + ins);
            }
            for (Address t : fl) {
                by.computeIfAbsent(t.getOffset(), k -> new ArrayList<>()).add(site);
            }
        }
        for (Map.Entry<Long, List<String>> e : by.entrySet()) {
            if (e.getKey() == -1L) {
                for (String s : e.getValue()) out.println("indirect   " + s);
            } else {
                out.println(where(toAddr(e.getKey())) + "   @ " + String.join(" ", e.getValue()));
            }
        }
    }

    private byte[] textBytes() throws Exception {
        if (textBytes == null) {
            textBytes = new byte[(int) text.getSize()];
            text.getBytes(text.getStart(), textBytes);
        }
        return textBytes;
    }

    private void calls(Address a) throws Exception {
        byte[] t = textBytes();
        long lo = text.getStart().getOffset();
        long target = a.getOffset();
        int n = 0;
        for (int i = 0; i + 5 <= t.length; i++) {
            int op = t[i] & 0xFF;
            if (op != 0xE8 && op != 0xE9) continue;
            int rel = (t[i + 1] & 0xFF) | (t[i + 2] & 0xFF) << 8 | (t[i + 3] & 0xFF) << 16
                | (t[i + 4] & 0xFF) << 24;
            if (((lo + i + 5 + rel) & 0xFFFFFFFFL) != target) continue;
            Address s = toAddr(lo + i);
            Instruction ins = currentProgram.getListing().getInstructionContaining(s);
            String seen = ins == null ? "not disassembled"
                : ins.getAddress().equals(s) ? "instruction" : "inside " + ins.getAddress();
            out.println((op == 0xE8 ? "call " : "jmp  ") + where(s) + "   (" + seen + ")");
            if (++n >= LIMIT) {
                out.println("... (cut)");
                break;
            }
        }
        if (n == 0) out.println("(none)");
    }

    // All initialized blocks, read once: Memory.findBytes is far slower than a plain scan.
    private List<Address> blkStart;
    private List<byte[]> blkBytes;

    private List<Address> search(byte[] pat, byte[] mask, int max) throws Exception {
        if (blkStart == null) {
            blkStart = new ArrayList<>();
            blkBytes = new ArrayList<>();
            for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
                if (!b.isInitialized() || b.getSize() > Integer.MAX_VALUE) continue;
                byte[] buf = new byte[(int) b.getSize()];
                b.getBytes(b.getStart(), buf);
                blkStart.add(b.getStart());
                blkBytes.add(buf);
            }
        }
        List<Address> hits = new ArrayList<>();
        int first = 0;
        while (mask != null && first < pat.length && mask[first] == 0) first++;
        if (first == pat.length) return hits;
        byte f = pat[first];
        for (int k = 0; k < blkBytes.size() && hits.size() < max; k++) {
            byte[] buf = blkBytes.get(k);
            int end = buf.length - pat.length;
            outer:
            for (int i = 0; i <= end; i++) {
                if (buf[i + first] != f) continue;
                for (int j = 0; j < pat.length; j++) {
                    if (mask != null && mask[j] == 0) continue;
                    if (buf[i + j] != pat[j]) continue outer;
                }
                hits.add(blkStart.get(k).add(i));
                if (hits.size() >= max) break;
            }
        }
        return hits;
    }

    private void findAll(byte[] pat, byte[] mask, String label) throws Exception {
        List<Address> hits = search(pat, mask, LIMIT + 1);
        int n = 0;
        for (Address h : hits) {
            if (++n > LIMIT) {
                out.println("... (cut at " + LIMIT + ")");
                break;
            }
            out.println(label + where(h));
        }
        if (n == 0) out.println("(none)");
    }

    private void scan(String hex) throws Exception {
        String h = hex.startsWith("0x") || hex.startsWith("0X") ? hex.substring(2) : hex;
        long v = Long.parseLong(h, 16);
        byte[] pat = { (byte) v, (byte) (v >> 8), (byte) (v >> 16), (byte) (v >> 24) };
        findAll(pat, null, "");
    }

    private void bytes(String spec) throws Exception {
        String[] parts = spec.replace(",", " ").trim().split("\\s+");
        byte[] pat = new byte[parts.length];
        byte[] mask = new byte[parts.length];
        for (int i = 0; i < parts.length; i++) {
            if (parts[i].startsWith("?")) {
                pat[i] = 0;
                mask[i] = 0;
            } else {
                pat[i] = (byte) Integer.parseInt(parts[i], 16);
                mask[i] = (byte) 0xFF;
            }
        }
        findAll(pat, mask, "");
    }

    private void str(String s) throws Exception {
        Memory m = currentProgram.getMemory();
        for (int pass = 0; pass < 2; pass++) {
            byte[] pat = pass == 0 ? s.getBytes(StandardCharsets.ISO_8859_1)
                : s.getBytes(StandardCharsets.UTF_16LE);
            int step = pass == 0 ? 1 : 2;
            int n = 0;
            Address lastStart = null;
            for (Address hit : search(pat, null, 200)) {
                if (n >= 60) {
                    out.println("... (cut at 60)");
                    break;
                }
                // walk back to the start of the string
                Address st = hit;
                for (int k = 0; k < 512; k++) {
                    Address p = st.subtract(step);
                    int c = pass == 0 ? m.getByte(p) & 0xFF
                        : (m.getByte(p) & 0xFF) | (m.getByte(p.add(1)) & 0xFF) << 8;
                    if (c < 0x20 || c > 0x7E) break;
                    st = p;
                }
                StringBuilder full = new StringBuilder();
                Address p = st;
                for (int k = 0; k < 200; k++) {
                    int c = pass == 0 ? m.getByte(p) & 0xFF
                        : (m.getByte(p) & 0xFF) | (m.getByte(p.add(1)) & 0xFF) << 8;
                    if (c == 0) break;
                    full.append(c >= 0x20 && c < 0x7F ? (char) c : '.');
                    p = p.add(step);
                }
                if (st.equals(lastStart)) continue; // same string again
                lastStart = st;
                n++;
                out.println((pass == 0 ? "ascii " : "utf16 ") + where(st) + "  \"" + full + "\"");
                ReferenceIterator it = currentProgram.getReferenceManager().getReferencesTo(st);
                int r = 0;
                while (it.hasNext() && r < 40) {
                    Reference ref = it.next();
                    out.println("    ref " + ref.getReferenceType() + " from " + where(ref.getFromAddress()));
                    r++;
                }
                if (r == 0) {
                    // no Ghidra reference: look for the raw pointer
                    long v = st.getOffset();
                    byte[] pp = { (byte) v, (byte) (v >> 8), (byte) (v >> 16), (byte) (v >> 24) };
                    int k = 0;
                    for (Address h2 : search(pp, null, 20)) {
                        out.println("    ptr at " + where(h2));
                        k++;
                    }
                    if (k == 0) out.println("    (no references)");
                }
            }
            if (n == 0) out.println((pass == 0 ? "ascii" : "utf16") + ": (none)");
        }
    }

    private void dd(Address a, int n) throws Exception {
        Memory m = currentProgram.getMemory();
        for (int i = 0; i < n; i++) {
            Address p = a.add(4L * i);
            long v = m.getInt(p) & 0xFFFFFFFFL;
            String what = "";
            Address t = toAddr(v);
            if (m.contains(t)) what = "  -> " + where(t);
            out.println(String.format("%08X +0x%03X  %08X%s", p.getOffset(), 4 * i, v, what));
        }
    }

    private void sym(String sub) {
        String s = sub.toLowerCase();
        int n = 0;
        SymbolIterator it = currentProgram.getSymbolTable().getAllSymbols(false);
        while (it.hasNext()) {
            Symbol y = it.next();
            if (y.getSource() == ghidra.program.model.symbol.SourceType.DEFAULT) continue;
            String q = y.getName(true);
            if (!q.toLowerCase().contains(s)) continue;
            out.println(String.format("0x%08X %s", y.getAddress().getOffset(), q));
            if (++n >= LIMIT) {
                out.println("... (cut)");
                break;
            }
        }
        if (n == 0) out.println("(none)");
    }
}
