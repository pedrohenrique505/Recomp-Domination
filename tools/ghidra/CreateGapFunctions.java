// Cria funções no código que a análise deixou sem dono (alcançado só por ponteiro, ex. callbacks).
// Para cada trecho indefinido em bloco executável: pula nops, desmonta e só cria a função se o
// fluxo desmontado tiver um "jr $ra" (descarta tabelas e constantes que desmontam por acaso).
// Uso headless: -process SCUS_971.77 -noanalysis -postScript CreateGapFunctions.java
// @category PS2Recomp

import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.address.AddressSetView;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.mem.MemoryBlock;

public class CreateGapFunctions extends GhidraScript {

    private int created = 0, rejected = 0;

    @Override
    public void run() throws Exception {
        // repete: cada função criada encolhe o trecho e pode revelar a próxima logo depois dela
        for (int pass = 1; pass <= 50 && !monitor.isCancelled(); pass++) {
            int before = created;
            scan();
            println("pass " + pass + ": +" + (created - before) + " functions");
            if (created == before) {
                break;
            }
        }
        println("Created " + created + " functions in gaps, rejected " + rejected + " ranges without jr $ra");
    }

    private void scan() throws Exception {
        for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
            // .vutext é microcódigo do VU, não código do EE
            if (!block.isExecute() || !block.isInitialized() || block.getName().startsWith(".vu")) {
                continue;
            }
            AddressSetView undefined = currentProgram.getListing().getUndefinedRanges(
                new AddressSet(block.getStart(), block.getEnd()), false, monitor);
            for (var range : undefined) {
                Address a = range.getMinAddress().getNewAddress((range.getMinAddress().getOffset() + 3) & ~3L);
                // lixo no início do trecho não esconde a função seguinte: tenta de 8 em 8 (alinhamento das funções)
                while (a.compareTo(range.getMaxAddress()) < 0) {
                    while (a.compareTo(range.getMaxAddress()) < 0 && getInt(a) == 0) {
                        a = a.add(4);
                    }
                    if (a.compareTo(range.getMaxAddress()) >= 0 || getFunctionContaining(a) != null) {
                        break;
                    }
                    DisassembleCommand dis = new DisassembleCommand(a, null, true);
                    if (dis.applyTo(currentProgram, monitor) && hasReturn(dis.getDisassembledAddressSet())) {
                        if (new CreateFunctionCmd(a).applyTo(currentProgram, monitor)) {
                            created++;
                        }
                        break; // o resto do trecho fica para a próxima passada
                    }
                    clearListing(dis.getDisassembledAddressSet());
                    rejected++;
                    a = a.getNewAddress((a.getOffset() + 8) & ~7L);
                }
            }
        }
    }

    private boolean hasReturn(AddressSetView body) {
        if (body == null || body.isEmpty()) {
            return false;
        }
        for (Instruction ins : currentProgram.getListing().getInstructions(body, true)) {
            if (ins.getMnemonicString().equals("jr") && ins.getDefaultOperandRepresentation(0).equals("ra")) {
                return true;
            }
        }
        return false;
    }
}
