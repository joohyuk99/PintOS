/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"

/* Project 2 */
uint64_t hash_func(const struct hash_elem *e, void *aux) {
	const struct page *p = hash_entry(e, struct page, elem);
	return hash_bytes(&p->va, sizeof(p->va));
}

bool less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux) {
	const struct page *pa = hash_entry(a, struct page, elem);
	const struct page *pb = hash_entry(b, struct page, elem);
	return pa->va < pb->va;
}

void hash_destructor(struct hash_elem *e, void *aux) {
	const struct page *p = hash_entry(e, struct page, elem);
	free(p);
}

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
	list_init(&frame_table);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */

		/* TODO: Insert the page into the spt. */
		struct page *page = (struct page*)malloc(sizeof(struct page));
		if(page == NULL)
			goto err;

		typedef bool (*initializerFunc)(struct page*, enum vm_type, void *);
		initializerFunc initializer = NULL;

		switch(VM_TYPE(type)) {
			case VM_ANON:
				initializer = anon_initializer;
				break;
			case VM_FILE:
				initializer = file_backed_initializer;
				break;
		}

		uninit_new(page, upage, init, type, aux, initializer);
		page->writable = writable;
		return spt_insert_page(spt, page);
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
// find va in spt, return va page
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {

	/* TODO: Fill this function. */
	struct page page;
	page.va = pg_round_down(va);
	struct hash_elem *e = hash_find(&spt->spt_hash, &page.elem);

	if(e != NULL)
		return hash_entry(e, struct page, elem);
	else
		return NULL;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {

	/* TODO: Fill this function. */
	if(!hash_insert(&spt->spt_hash, &page->elem))
		return true;
	
	return false;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	
	/* TODO: The policy for eviction is up to you. */
	struct frame *victim = NULL;
	struct thread *curr = thread_current();
	
	// second-chance algorithm
	struct list_elem *e;
	for(e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e)) {
		victim = list_entry(e, struct frame, elem);
		if(pml4_is_accessed(curr->pml4, victim->page->va))  // check frame is accessed recently
			pml4_set_accessed(curr->pml4, victim->page->va, false);  // if accessed, init access bit (false)
		else  // else return victim frame
			return victim;
	}

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */
	swap_out(victim->page);

	return victim;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	
	/* TODO: Fill this function. */
	struct frame *frame = (struct frame*)malloc(sizeof(struct frame));
	frame->kva = palloc_get_page(PAL_USER);  // allocate virtual memory

	if(frame->kva == NULL) {
		frame = vm_evict_frame();  // swap out
		frame->page = NULL;  // unlink with virtual memory
		return frame;
	}
	
	frame->page = NULL;
	// if allocate success, add frame into frame table
	list_push_back(&frame_table, &frame->elem);

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {

	/* TODO: Validate the fault */
	/* TODO: Your code goes here */

	struct supplemental_page_table *spt = &thread_current()->spt;
	struct page *page = NULL;

	if(addr == NULL || is_kernel_vaddr(addr))
		return false;
	
	if(not_present) {
		page = spt_find_page(spt, addr);
		if(page == NULL)
			return false;
		if(write == 1 && page->writable == 0)
			return false;
		return vm_do_claim_page(page);
	}

	return false;
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va UNUSED) {
	
	/* TODO: Fill this function */
	struct page *page = spt_find_page(&thread_current()->spt, va);

	if(page == NULL)
		return false;

	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {

	struct frame *frame = vm_get_frame ();  // allocate frame

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	// mapping virtual address - physical address and set writable
	pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->writable);

	return swap_in (page, frame->kva);  // restore data in swap area
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	hash_init(&spt->spt_hash, hash_func, less_func, NULL);
}

/* Copy supplemental page table from src to dst */
// for fork system call
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
	
	struct hash_iterator *iter;
	struct page *src_page, *dst_page;
	enum vm_type type;
	void *upage;
	bool writable;

	hash_first(iter, &src->spt_hash);
	for(; iter != NULL; hash_next(iter)) {
		src_page = hash_entry(hash_cur(iter), struct page, elem);
		type = src_page->operations->type;
		upage = src_page->va;
		writable = src_page->writable;

		if(type == VM_UNINIT) {  // if page is not yet init
			if(!vm_alloc_page_with_initializer(page_get_type(src_page),
					src_page->va, src_page->writable, src_page->uninit.init, src_page->uninit.aux))
				return false;
		}

		else if(type == VM_FILE) {  // if page is file
			if(!vm_alloc_page_with_initializer(type, upage, writable, NULL, &src_page->file))
				return false;
			
			dst_page = spt_find_page(dst, upage);  // copy data (memory->VM page)
			if(!file_backed_initializer(dst_page, type, NULL))
				return false;
			
			dst_page->frame = src_page->frame;
			if(!pml4_set_page(thread_current()->pml4, dst_page->va, src_page->frame->kva, src_page->writable))
				return false;
		}

		else if(type == VM_ANON) {  // if page is annonymous page
			if(!vm_alloc_page(type, upage, writable))  // allocate and init page
				return false;
			
			// if(!vm_copy_claim_page(dst, upage, src_page->frame->kva, writable))
			// 	return false;
		}

		else
			return false;
	}

	return true;
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */

	// struct hash_interator *iter;
	// struct page *page = NULL;
	// hash_first(iter, &spt->spt_hash);
	// for(; iter != NULL; iter = hash_next(iter)) {
	// 	page = hash_entry(hash_cur(iter), struct page, elem);
	// 	if(page->operations->type == VM_FILE)
	// 		do_munmap(page->va);
	// }

	hash_destroy(&spt->spt_hash, hash_destructor);
}
