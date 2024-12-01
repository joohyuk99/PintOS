/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "bitmap.h"
#include <stdlib.h>
#include "threads/mmu.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

struct bitmap *swap_table;
size_t swap_size;

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	swap_disk = disk_get(1, 1);
	swap_size = disk_size(swap_disk);
	swap_table = bitmap_create(swap_size);
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	struct uninit_page *uninit = &page->uninit;
    memset(uninit, 0, sizeof(struct uninit_page));

	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;
	anon_page->sector = BITMAP_ERROR;

	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;
	size_t slot = anon_page->sector;
	size_t sector = slot * (PGSIZE / DISK_SECTOR_SIZE);
	

	if(slot == BITMAP_ERROR || !bitmap_test(swap_table, slot))
		return false;

	bitmap_set(swap_table, slot, false);

	for(size_t i = 0; i < SECTOR_SIZE; i++)
		disk_read(swap_disk, sector + i, kva + DISK_SECTOR_SIZE * i);

	sector = BITMAP_ERROR;

	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;

	size_t free_idx = bitmap_scan_and_flip(swap_table, 0, 1, false);

	if(free_idx == BITMAP_ERROR)
		return false;
	
	size_t sector = free_idx * SECTOR_SIZE;

	for(size_t i = 0; i < SECTOR_SIZE; i++)
		disk_write(swap_disk, sector + i, page->va + DISK_SECTOR_SIZE * i);

	anon_page->sector = sector;
	page->frame->page = NULL;
	page->frame = NULL;

	pml4_clear_page(thread_current()->pml4, page->va);

	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;

	if(anon_page->sector != BITMAP_ERROR)
		bitmap_reset(swap_table, anon_page->sector);

	if(page->frame != NULL) {
		list_remove(&page->frame->elem);
		page->frame->page = NULL;
		free(page->frame);
		page->frame = NULL;
	}

	pml4_clear_page(thread_current()->pml4, page->va);
}
