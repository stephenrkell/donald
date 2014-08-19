open Elf
open Elf_header
open Elf_section
open Elf_program_header_table
open Elf_symbol_tables
open Error_monad
open Unix

type mapping = {
    base : int64;
    size : int64;
    file : (string * int64) option;
    perms : bool * bool * bool * bool
}

type region = {
    m : mapping;
    diffs : (int64 * (char * char) list) list
}

type image = region list * elf list

type req = 
| File of string
| Data of char list

type policy = | Null

type state = | NoState

(* A policy is a thing that we can ask questions of, like 
 * 
 * - what is the value of symbol "x" (as seen from section y offset z)? 
 * 
 * to get an answer which we use to build a mapping. *)

let elf_relocate_mapping (e: elf) (ma: mapping) =
    { m = ma; diffs = [] } (* FIXME *)

let elf_new_image (p : policy) (filename: string) (s : state) = 
    let e = try
        let bitstream = Bitstring.bitstring_of_file filename in
        elf_of_bitstring bitstream
    with _ ->
        failwith "cannot open file"
    in
    (* use the program headers *)
    match e with 
     { ehdr; shdrs; pht = Some(phs)} -> begin
            (* collect the mappings specified by the pht *)
            let mappings_of_phdr (ph : elf_program_header_entry) = 
                let perms_of_phdr_flags flags = 
                    (((flags land (* PF_R *) 4) != 0), 
                     ((flags land (* PF_W *) 2) != 0), 
                     ((flags land (* PF_X *) 1) != 0),
                     false)
                in
                if ph.p_type != (* PT_LOAD *) (Int64.of_int 1) then []
                else if ph.p_filesz = (Int64.of_int 0) then [
                    {   base = ph.p_vaddr;
                        size = ph.p_memsz; 
                        file = None;
                        perms = (perms_of_phdr_flags (Int64.to_int ph.p_flags));
                    }]
                else if ph.p_memsz = ph.p_filesz then [
                    {   base = ph.p_vaddr;
                        size = ph.p_memsz; 
                        file = Some(filename, ph.p_offset);
                        perms = (perms_of_phdr_flags (Int64.to_int ph.p_flags));
                    }]
                else if ph.p_memsz > ph.p_filesz then [
                    {   base = ph.p_vaddr;
                        size = ph.p_filesz; 
                        file = Some(filename, ph.p_offset);
                        perms = (perms_of_phdr_flags (Int64.to_int ph.p_flags));
                    };
                    {   base = Int64.add ph.p_vaddr (* + *) ph.p_filesz; 
                        size = Int64.sub ph.p_memsz (* - *) ph.p_filesz; 
                        file = None; 
                        perms = (perms_of_phdr_flags (Int64.to_int ph.p_flags));
                    }]
                else failwith "invalid phdr mapping"
            in
            let rec collect_mappings ms phdrs = begin
                match phdrs with 
                    [] -> ms
                  | ph :: phdrs -> (mappings_of_phdr ph) @ (collect_mappings ms phdrs)
            end
            in
            let rec relocated_regions_of_mappings allms = 
                match allms with
                    [] -> []
                  | m :: ms -> (elf_relocate_mapping e m) :: (relocated_regions_of_mappings ms)
            in 
            (relocated_regions_of_mappings (collect_mappings [] phs), [e])
         end
       | _ -> failwith "ELF file has no program headers"

external load_one: ((* base_addr : *) int64 * (* fd : *) int
            * (* vaddr : *) int64 * (* offset : *) int64
            * (* memsz : *) int64 * (* filesz : *) int64
            * (* read : *) bool * (* write : *) bool * (* exec : *) bool ) -> unit = "caml_load"

(* HACK HACK HACK! *)
external int_of_filedesc: file_descr -> int = "%identity"

let load (im : image) (base_addr: int64) =
    let get_fd f = match f with
        None -> -1
      | Some(filename, offset) -> int_of_filedesc (openfile filename [O_RDONLY] 0660)
    in
    let regions, elfs = im
    in
    let rec load_all allrs = 
        match allrs with
            [] -> ()
          | r :: rs ->
                let fd = get_fd r.m.file
                in
                let offset = if fd = -1 then (Int64.of_int 0) else match r.m.file with 
                    Some(filename, off) -> off
                  | None -> failwith "failed to open file"
                in
                let read, write, exec, _ = r.m.perms 
                in
                load_one (base_addr, fd, r.m.base, offset, r.m.size, (if fd = -1 then (Int64.of_int 0) else r.m.size), read, write, exec)
                ;
                load_all rs
    in load_all regions

external enter : (* addr: *) int64 -> unit = "caml_enter"

let link (p : policy) (im: image) (r: req) (s : state) = 
    match (r, im) with
        (File(fn), ([], [])) -> elf_new_image p fn s
      | _ -> failwith "invalid request"

(* donald's main is going to do the following  *)
let filename = Array.get Sys.argv 1 
in
let r = File(filename)
in
let image = link Null ([], []) r NoState
in
let _, elfs = image
in
let entry = match elfs with e :: es -> e.ehdr.e_entry | _ -> failwith "No executable in image"
in 
load image (Int64.of_int 0); enter entry
