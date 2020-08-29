open Elf_file1
open Elf_linking_file2
open Elf_linking_file3
open Elf_header
open Elf_program_header_table
open Elf_symbol_table
open Error
open Unix

type mapping = {
    base : Uint64.t;
    size : Uint64.t;
    file : (string * Uint64.t) option;
    perms : bool * bool * bool * bool
}

type region = {
    m : mapping;
    diffs : (Uint64.t * (char * char) list) list
}

type image = region list * elf64_linking_file3 list

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

let elf_relocate_mapping (e: elf64_linking_file3) (ma: mapping) =
    { m = ma; diffs = [] } (* FIXME *)

let elf_new_image (p : policy) (filename: string) (s : state) = 
    let e = try
        let bitstream = Bitstring.bitstring_of_file filename in
        read_elf64_linking_file3 bitstream
    with _ ->
        failwith "cannot open file"
    in
    let elf = match e with Success(it) -> it | Fail(str) -> failwith ("error: " ^ str)
    in 
    (* use the program headers *)
    match elf with 
     { elf64_linking_file3_header
     ; elf64_linking_file3_program_header_table = Some(phs)
     ; elf64_linking_file3_body
     ; elf64_linking_file3_section_header_table 
     } -> begin
            (* collect the mappings specified by the pht *)
            let mappings_of_phdr (ph : elf64_program_header_table_entry) = 
                let perms_of_phdr_flags flags = 
                    (((flags land (* PF_R *) 4) != 0), 
                     ((flags land (* PF_W *) 2) != 0), 
                     ((flags land (* PF_X *) 1) != 0),
                     false)
                in
                if ph.elf64_p_type != (* PT_LOAD *) (Uint32.of_int 1) then []
                else if ph.elf64_p_filesz = (Uint64.of_int 0) then [
                    {   base = ph.elf64_p_vaddr;
                        size = ph.elf64_p_memsz; 
                        file = None;
                        perms = (perms_of_phdr_flags (Uint32.to_int ph.elf64_p_flags));
                    }]
                else if ph.elf64_p_memsz = ph.elf64_p_filesz then [
                    {   base = ph.elf64_p_vaddr;
                        size = ph.elf64_p_memsz; 
                        file = Some(filename, ph.elf64_p_offset);
                        perms = (perms_of_phdr_flags (Uint32.to_int ph.elf64_p_flags));
                    }]
                else if ph.elf64_p_memsz > ph.elf64_p_filesz then [
                    {   base = ph.elf64_p_vaddr;
                        size = ph.elf64_p_filesz; 
                        file = Some(filename, ph.elf64_p_offset);
                        perms = (perms_of_phdr_flags (Uint32.to_int ph.elf64_p_flags));
                    };
                    {   base = Uint64.add ph.elf64_p_vaddr (* + *) ph.elf64_p_filesz; 
                        size = Uint64.sub ph.elf64_p_memsz (* - *) ph.elf64_p_filesz; 
                        file = None; 
                        perms = (perms_of_phdr_flags (Uint32.to_int ph.elf64_p_flags));
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
                  | m :: ms -> (elf_relocate_mapping elf m) :: (relocated_regions_of_mappings ms)
            in 
            (relocated_regions_of_mappings (collect_mappings [] phs), [elf])
         end
       | _ -> failwith "ELF file has no program headers"

external load_one: ((* base_addr : *) Uint64.t * (* fd : *) int
            * (* vaddr : *) Uint64.t * (* offset : *) Uint64.t
            * (* memsz : *) Uint64.t * (* filesz : *) Uint64.t
            * (* read : *) bool * (* write : *) bool * (* exec : *) bool ) -> unit = "caml_load"

(* HACK HACK HACK! *)
external int_of_filedesc: file_descr -> int = "%identity"

let load (im : image) (base_addr: Uint64.t) =
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
                let offset = if fd = -1 then (Uint64.of_int 0) else match r.m.file with 
                    Some(filename, off) -> off
                  | None -> failwith "failed to open file"
                in
                let read, write, exec, _ = r.m.perms 
                in
                load_one (base_addr, fd, r.m.base, offset, r.m.size, 
                           (if fd = -1 then (Uint64.of_int 0) else r.m.size), 
                           read, write, exec)
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
let entry = match elfs with e :: es -> e.elf64_linking_file3_header.elf64_entry | _ -> failwith "No executable in image"
in 
load image (Uint64.of_int 0); enter (Uint64.to_int64 entry)
