	uintptr_t *p = (uintptr_t *)sp_on_entry;
	uintptr_t argc = *p++;
	char **address_of_argv0 = (char**)p++;
	while (*p++);
	char **address_of_envp0 = (char**)p++;
	while (*p++);
	void **address_of_auxv0 = (void**)p;
	/* What do we want to get out of auxv? At least AT_BASE. */
	ElfW(auxv_t) *p_auxv_start = (void*) p;
	ElfW(auxv_t) *p_auxv = p_auxv_start;
	p = 0; // finished with p! we'll use p_auxv from now on
	ElfW(Phdr) *phdrs_addr = 0;
	uintptr_t phdr_size = 0ul;
	uintptr_t phdrs_num = 0ul;
	
	/* uClibc scans auxv for a nonnull interpreter address, in case the kernel
	 * set it up as our base address. But it doesn't (for me), and doing so would 
	 * clearly be wrong: the *interpreter* address will be null because we have no
	 * interpreter. We *are* the interpreter! 
	 * 
	 * Instead we need to get our own load address by some other means, and
	 * *write* it into the auxv. uClibc falls back to doing this via the _begin
	 * symbol inserted in the linker script. We do the same (much as I'd rather
	 * use PT_PHDR -- see below). */
	while (p_auxv->a_type != AT_NULL)
	{
		if (p_auxv->a_type == AT_PHNUM) phdrs_num = p_auxv->a_un.a_val;
		else if (p_auxv->a_type == AT_PHDR) phdrs_addr = (void*) p_auxv->a_un.a_val;
		else if (p_auxv->a_type == AT_PHENT) phdr_size = p_auxv->a_un.a_val;
		p_auxv++;
	}
	/* If we had a PT_PHDR, things would be very easy. But we don't because
	 * the linker only creates that if we have an .interp section, which we
	 * must not have. So we rely on the hacked linker script's _begin symbol. */
	unsigned char *base_addr = 0ul;
	for (int i_phdr = 0; i_phdr != phdrs_num; ++i_phdr)
	{
		if (phdrs_addr[i_phdr].p_type == PT_DYNAMIC)
		{
			uintptr_t begin_tmp = (uintptr_t) &_begin;
			base_addr = (unsigned char *) /* &phdrs_addr[i_phdr]*/ &_DYNAMIC - (unsigned long) phdrs_addr[i_phdr].p_vaddr;
		}
	}
	/* If we have a PT_PHDR, things are very easy. */
	if (!base_addr)
	{
		for (int i_phdr = 0; i_phdr != phdrs_num; ++i_phdr)
		{
			if (phdrs_addr[i_phdr].p_type == PT_DYNAMIC)
			{
				base_addr = (unsigned char *) /* &phdrs_addr[i_phdr]*/ &_DYNAMIC - (unsigned long) phdrs_addr[i_phdr].p_vaddr;
			}
		}
	}

	
	
	
	
	
	
		
	// fix up the auxv so that it includes our base address
	p_auxv = p_auxv_start;
	while (p_auxv->a_type != AT_NULL)
	{
		if (p_auxv->a_type == AT_BASE) p_auxv->a_un.a_val = (uintptr_t) base_addr;
		p_auxv++;
	}
