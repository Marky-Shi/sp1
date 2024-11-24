use std::borrow::BorrowMut;

use p3_field::PrimeField32;
use p3_matrix::dense::RowMajorMatrix;
use rayon::iter::{ParallelBridge, ParallelIterator};
use sp1_core_executor::{
    events::{ByteLookupEvent, InstrEvent},
    ExecutionRecord, Program,
};
use sp1_stark::air::MachineAir;

use crate::utils::{next_power_of_two, zeroed_f_vec};

use super::{
    columns::{MemoryInstructionsColumns, NUM_MEMORY_INSTRUCTIONS_COLUMNS},
    MemoryInstructionsChip,
};

impl<F: PrimeField32> MachineAir<F> for MemoryInstructionsChip {
    type Record = ExecutionRecord;

    type Program = Program;

    fn name(&self) -> String {
        "MemoryInstructions".to_string()
    }

    fn generate_trace(
        &self,
        input: &ExecutionRecord,
        _: &mut ExecutionRecord,
    ) -> RowMajorMatrix<F> {
        let chunk_size = std::cmp::max((input.memory_instr_events.len()) / num_cpus::get(), 1);
        let nb_rows = input.memory_instr_events.len();
        let size_log2 = input.fixed_log2_rows::<F, _>(self);
        let padded_nb_rows = next_power_of_two(nb_rows, size_log2);
        let mut values = zeroed_f_vec(padded_nb_rows * NUM_MEMORY_INSTRUCTIONS_COLUMNS);

        values
            .chunks_mut(chunk_size * NUM_MEMORY_INSTRUCTIONS_COLUMNS)
            .enumerate()
            .par_bridge()
            .for_each(|(i, rows)| {
                rows.chunks_mut(NUM_MEMORY_INSTRUCTIONS_COLUMNS).enumerate().for_each(
                    |(j, row)| {
                        let idx = i * chunk_size + j;
                        let cols: &mut MemoryInstructionsColumns<F> = row.borrow_mut();

                        if idx < input.memory_instr_events.len() {
                            let mut byte_lookup_events = Vec::new();
                            let event = &input.memory_instr_events[idx];
                            self.event_to_row(event, cols, &mut byte_lookup_events);
                        }
                    },
                );
            });

        // Convert the trace to a row major matrix.
        RowMajorMatrix::new(values, NUM_MEMORY_INSTRUCTIONS_COLUMNS)
    }

    fn included(&self, shard: &Self::Record) -> bool {
        if let Some(shape) = shard.shape.as_ref() {
            shape.included::<F, _>(self)
        } else {
            shard.contains_memory()
        }
    }
}

impl MemoryInstructionsChip {
    fn event_to_row<F: PrimeField32>(
        &self,
        event: &InstrEvent,
        cols: &mut MemoryInstructionsColumns<F>,
        byte_lookup_events: &mut Vec<ByteLookupEvent>,
    ) {
        // Populate memory accesses for reading from memory.
        cols.memory_access
            .populate(event.mem_access.expect("Memory access is required"), byte_lookup_events);

        // Populate addr_word and addr_aligned columns.
        let memory_addr = event.b.wrapping_add(event.c);
        let aligned_addr = memory_addr - memory_addr % WORD_SIZE as u32;
        cols.addr_word = memory_addr.into();
        cols.addr_word_range_checker.populate(memory_addr);
        cols.addr_aligned = F::from_canonical_u32(aligned_addr);

        // Populate the aa_least_sig_byte_decomp columns.
        assert!(aligned_addr % 4 == 0);
        let aligned_addr_ls_byte = (aligned_addr & 0x000000FF) as u8;
        let bits: [bool; 8] = array::from_fn(|i| aligned_addr_ls_byte & (1 << i) != 0);
        cols.aa_least_sig_byte_decomp = array::from_fn(|i| F::from_bool(bits[i + 2]));
        cols.addr_word_nonce = F::from_canonical_u32(
            nonce_lookup.get(event.memory_add_lookup_id.0 as usize).copied().unwrap_or_default(),
        );

        // Populate memory offsets.
        let addr_offset = (memory_addr % WORD_SIZE as u32) as u8;
        cols.addr_offset = F::from_canonical_u8(addr_offset);
        cols.offset_is_one = F::from_bool(addr_offset == 1);
        cols.offset_is_two = F::from_bool(addr_offset == 2);
        cols.offset_is_three = F::from_bool(addr_offset == 3);

        // If it is a load instruction, set the unsigned_mem_val column.
        let mem_value = event.memory_record.unwrap().value();
        if matches!(event.opcode, Opcode::LB | Opcode::LBU | Opcode::LH | Opcode::LHU | Opcode::LW)
        {
            match instruction.opcode {
                Opcode::LB | Opcode::LBU => {
                    cols.unsigned_mem_val =
                        (mem_value.to_le_bytes()[addr_offset as usize] as u32).into();
                }
                Opcode::LH | Opcode::LHU => {
                    let value = match (addr_offset >> 1) % 2 {
                        0 => mem_value & 0x0000FFFF,
                        1 => (mem_value & 0xFFFF0000) >> 16,
                        _ => unreachable!(),
                    };
                    cols.unsigned_mem_val = value.into();
                }
                Opcode::LW => {
                    cols.unsigned_mem_val = mem_value.into();
                }
                _ => unreachable!(),
            }

            // For the signed load instructions, we need to check if the loaded value is negative.
            if matches!(event.opcode, Opcode::LB | Opcode::LH) {
                let most_sig_mem_value_byte = if matches!(instruction.opcode, Opcode::LB) {
                    cols.unsigned_mem_val.to_u32().to_le_bytes()[0]
                } else {
                    cols.unsigned_mem_val.to_u32().to_le_bytes()[1]
                };

                for i in (0..8).rev() {
                    memory_columns.most_sig_byte_decomp[i] =
                        F::from_canonical_u8(most_sig_mem_value_byte >> i & 0x01);
                }
                if memory_columns.most_sig_byte_decomp[7] == F::one() {
                    cols.mem_value_is_neg_not_x0 = F::from_bool(instruction.op_a != (X0 as u8));
                    cols.unsigned_mem_val_nonce = F::from_canonical_u32(
                        nonce_lookup
                            .get(event.memory_sub_lookup_id.0 as usize)
                            .copied()
                            .unwrap_or_default(),
                    );
                }
            }

            // Set the `mem_value_is_pos_not_x0` composite flag.
            cols.mem_value_is_pos_not_x0 = F::from_bool(
                ((matches!(instruction.opcode, Opcode::LB | Opcode::LH)
                    && (memory_columns.most_sig_byte_decomp[7] == F::zero()))
                    || matches!(instruction.opcode, Opcode::LBU | Opcode::LHU | Opcode::LW))
                    && instruction.op_a != (X0 as u8),
            );
        }

        // Add event to byte lookup for byte range checking each byte in the memory addr
        let addr_bytes = memory_addr.to_le_bytes();
        for byte_pair in addr_bytes.chunks_exact(2) {
            byte_lookup_events.add_byte_lookup_event(ByteLookupEvent {
                opcode: ByteOpcode::U8Range,
                a1: 0,
                a2: 0,
                b: byte_pair[0],
                c: byte_pair[1],
            });
        }
    }
}
