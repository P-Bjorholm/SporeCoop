// Prepares a SporeCoop Ghidra project after auto-analysis (headless post-script).
//
//   analyzeHeadless <dir> spore -process SporeApp.exe -noanalysis -scriptPath tools/ghidra \
//       -postScript SporeSetup.java [nodisc] <file.sym>...
//
// 1. Function discovery Ghidra's analysers miss (docs/05-re-notes.md 5.2): targets of E8 call rel32
//    found in bytes Ghidra did not disassemble, and code pointers stored in .rdata/.data (vtables,
//    callback tables). A target becomes a function only if it is a plausible MSVC function start
//    (after CC/C3/90 padding or a `ret imm16`), not inside an existing function, and disassembles.
//    After creating them, the analysers run again on the changes. "nodisc" skips this step.
// 2. Names: every `Name=0xADDR ; note` line of the given .sym files becomes the primary symbol at
//    that address, in its C++ namespace (Simulator::cGameDataUFO::TakeDamage), with the note as a
//    plate comment. Later files win, so pass base files first and overrides last. Entries marked
//    `; low` (unsure ports) are skipped. Symbols in .text with no function get one.
//
// @category SporeCoop
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.app.util.NamespaceUtils;
import ghidra.app.util.SymbolPath;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.CommentType;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Namespace;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;

import java.io.BufferedReader;
import java.io.FileReader;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

public class SporeSetup extends GhidraScript {

    private MemoryBlock text;
    private byte[] textBytes;
    private long textLo, textHi;

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        boolean discover = true;
        List<String> syms = new ArrayList<>();
        for (String a : args) {
            if (a.equals("nodisc")) discover = false;
            else syms.add(a);
        }
        Memory mem = currentProgram.getMemory();
        text = mem.getBlock(".text");
        if (text == null) throw new Exception("no .text block");
        textLo = text.getStart().getOffset();
        textHi = text.getEnd().getOffset() + 1;
        textBytes = new byte[(int) text.getSize()];
        text.getBytes(text.getStart(), textBytes);

        Map<Address, List<String[]>> names = readSyms(syms);
        println("symbols to apply: " + entries + " at " + names.size() + " addresses");
        int created = createSymFunctions(names);
        println("functions created at symbol addresses: " + created);

        if (discover) {
            int before = currentProgram.getFunctionManager().getFunctionCount();
            int n1 = discoverCallTargets();
            int n2 = discoverDataPointers();
            println("discovery: " + n1 + " call rel32 targets, " + n2 + " data pointer targets");
            if (n1 + n2 > 0) {
                long t = System.currentTimeMillis();
                analyzeChanges(currentProgram);
                println("re-analysis took " + (System.currentTimeMillis() - t) / 1000 + " s");
            }
            int after = currentProgram.getFunctionManager().getFunctionCount();
            println("functions: " + before + " -> " + after);
        }
        int applied = applyNames(names);
        println("names applied: " + applied + " of " + entries);
        println("functions total: " + currentProgram.getFunctionManager().getFunctionCount());
    }

    // ---- .sym files -------------------------------------------------------------------------

    private int entries;

    // address -> every entry naming it, in file order (the last one wins the primary name)
    private Map<Address, List<String[]>> readSyms(List<String> files) throws Exception {
        Map<Address, List<String[]>> out = new LinkedHashMap<>();
        for (String f : files) {
            int n = 0;
            String file = f.substring(f.lastIndexOf('/') + 1);
            try (BufferedReader r = new BufferedReader(new FileReader(f))) {
                String line;
                while ((line = r.readLine()) != null) {
                    line = line.trim();
                    if (line.isEmpty() || line.startsWith("#")) continue;
                    int eq = line.indexOf("=0x");
                    if (eq <= 0) continue;
                    String name = line.substring(0, eq).trim();
                    String rest = line.substring(eq + 1);
                    String note = "";
                    int semi = rest.indexOf(';');
                    if (semi >= 0) {
                        note = rest.substring(semi + 1).trim();
                        rest = rest.substring(0, semi);
                    }
                    if (note.equals("low") || note.startsWith("low ")) continue;
                    long va;
                    try {
                        va = Long.parseLong(rest.trim().substring(2), 16);
                    } catch (Exception e) {
                        continue;
                    }
                    Address a = toAddr(va);
                    if (!currentProgram.getMemory().contains(a)) continue;
                    out.computeIfAbsent(a, k -> new ArrayList<>()).add(new String[] { name, note, file });
                    n++;
                }
            }
            entries += n;
            println(f + ": " + n + " entries");
        }
        return out;
    }

    private int createSymFunctions(Map<Address, List<String[]>> names) {
        int n = 0;
        for (Address a : names.keySet()) {
            if (!text.contains(a)) continue;
            if (getFunctionContaining(a) != null) continue;
            if (makeFunction(a)) n++;
        }
        return n;
    }

    private Namespace namespaceOf(SymbolPath sp) throws Exception {
        if (sp.getParent() == null) return currentProgram.getGlobalNamespace();
        return NamespaceUtils.createNamespaceHierarchy(sp.getParent().getPath(), null,
            currentProgram, SourceType.USER_DEFINED);
    }

    private int applyNames(Map<Address, List<String[]>> names) {
        SymbolTable st = currentProgram.getSymbolTable();
        int n = 0;
        for (Map.Entry<Address, List<String[]>> e : names.entrySet()) {
            Address a = e.getKey();
            List<String[]> list = e.getValue();
            StringBuilder plate = new StringBuilder();
            for (int i = 0; i < list.size(); i++) {
                String[] en = list.get(i);
                String full = en[0].replaceAll("\\s+", "_");
                boolean winner = i == list.size() - 1;
                try {
                    SymbolPath sp = new SymbolPath(full);
                    Namespace ns = namespaceOf(sp);
                    Function f = getFunctionAt(a);
                    if (winner && f != null) {
                        f.setParentNamespace(ns);
                        f.setName(sp.getName(), SourceType.USER_DEFINED);
                    } else {
                        Symbol s = st.getSymbol(sp.getName(), a, ns);
                        if (s == null) s = st.createLabel(a, sp.getName(), ns, SourceType.USER_DEFINED);
                        if (winner) s.setPrimary();
                    }
                    n++;
                } catch (Exception ex) {
                    println("name failed at " + a + " " + full + ": " + ex.getMessage());
                }
                if (plate.length() > 0) plate.append('\n');
                plate.append(en[2]).append(": ").append(en[0]);
                if (!en[1].isEmpty()) plate.append(" ; ").append(en[1]);
            }
            currentProgram.getListing().setComment(a, CommentType.PLATE, plate.toString());
        }
        return n;
    }

    // ---- discovery --------------------------------------------------------------------------

    private int tb(long va) {
        return textBytes[(int) (va - textLo)] & 0xFF;
    }

    private boolean plausibleStart(long va) {
        if (va - 3 < textLo || va >= textHi) return false;
        int prev = tb(va - 1);
        if (prev == 0xCC || prev == 0xC3 || prev == 0x90) return true;
        return tb(va - 3) == 0xC2;
    }

    // A site at `va` counts when it is not covered by code Ghidra already knows (then Ghidra
    // would have seen the call itself) and not inside defined data.
    private boolean undefinedAt(Address a) {
        Listing l = currentProgram.getListing();
        return l.getInstructionContaining(a) == null && l.getDefinedDataContaining(a) == null;
    }

    private boolean candidateTarget(long va) {
        if (va < textLo || va >= textHi) return false;
        Address a = toAddr(va);
        if (getFunctionContaining(a) != null) return false;
        if (currentProgram.getListing().getDefinedDataContaining(a) != null) return false;
        Instruction ins = currentProgram.getListing().getInstructionContaining(a);
        if (ins != null && !ins.getAddress().equals(a)) return false;
        return plausibleStart(va);
    }

    private int discoverCallTargets() {
        Map<Long, Integer> count = new HashMap<>();
        for (int i = 0; i + 5 <= textBytes.length; i++) {
            if ((textBytes[i] & 0xFF) != 0xE8) continue;
            int rel = (textBytes[i + 1] & 0xFF) | (textBytes[i + 2] & 0xFF) << 8
                | (textBytes[i + 3] & 0xFF) << 16 | (textBytes[i + 4] & 0xFF) << 24;
            long site = textLo + i;
            long tgt = (site + 5 + rel) & 0xFFFFFFFFL;
            if (tgt < textLo || tgt >= textHi) continue;
            if (!undefinedAt(toAddr(site))) continue;
            count.merge(tgt, 1, Integer::sum);
        }
        int n = 0;
        for (Map.Entry<Long, Integer> e : new TreeMap<>(count).entrySet()) {
            if (monitor.isCancelled()) break;
            if (!candidateTarget(e.getKey())) continue;
            if (makeFunction(toAddr(e.getKey()))) n++;
        }
        println("call rel32 sites in undisassembled bytes: " + count.size() + " distinct targets");
        return n;
    }

    private int discoverDataPointers() throws Exception {
        int n = 0, seen = 0;
        for (String bn : new String[] { ".rdata", ".data" }) {
            MemoryBlock b = currentProgram.getMemory().getBlock(bn);
            if (b == null || !b.isInitialized()) continue;
            byte[] buf = new byte[(int) b.getSize()];
            b.getBytes(b.getStart(), buf);
            for (int off = 0; off + 4 <= buf.length; off += 4) {
                long v = (buf[off] & 0xFF) | (buf[off + 1] & 0xFF) << 8 | (buf[off + 2] & 0xFF) << 16
                    | ((long) (buf[off + 3] & 0xFF)) << 24;
                if (v < textLo || v >= textHi) continue;
                if (!candidateTarget(v)) continue;
                seen++;
                if (makeFunction(toAddr(v))) n++;
            }
        }
        println("data pointers to plausible starts outside functions: " + seen);
        return n;
    }

    private boolean makeFunction(Address a) {
        Listing l = currentProgram.getListing();
        if (l.getInstructionAt(a) == null) {
            DisassembleCommand dc = new DisassembleCommand(a, null, true);
            if (!dc.applyTo(currentProgram, monitor)) return false;
            if (l.getInstructionAt(a) == null) return false;
        }
        if (getFunctionAt(a) != null) return false;
        CreateFunctionCmd cf = new CreateFunctionCmd(a);
        return cf.applyTo(currentProgram, monitor) && getFunctionAt(a) != null;
    }
}
