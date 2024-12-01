# Project 2: User Program

1. argument Passing

2. User Memory

3. System Call

## Argument Passing

### User Program 실행 과정

1. `threads/init.c/main()`

2. `run_actions(argv)` → `run_task(char **argv)`

3. `process_wait(process_create_initd(task))` → `thread_create()`

4. `process_create_initd`에서 실행 프로그램 이름 pasing, `thread_create` 실행

5. `thread_create`에서 `initd` 함수 thread 생성 후 scheduler 호출

6. `initd`에서 프로세스 초기화 후 `process_exec` 호출

### Argument Parsing & thread context

- `process_exec` 함수에서 argument pasing 후  `load` 함수 호출

- `load`

  - thread의 page directory 생성 / 활성화

  - 실행파일 열기

  - 실행파일 잠금 및 `setup_stack` 함수 호출하여 stack 설정

    - 초기 메모리 세팅 / page table(pml4) 세팅

    - `*rsp` → 초기 stack pointer 설정

  - `*rip` → 실행파일 진입점 (virtual memory address)

### Argument Passing

- pasing된 argument를 rsp에서부터 시작하여 규칙에 맞게 적재

## User Memory

- system call 호출 시 parameter로 제공된 포인터의 주소가 올바른지 검증 필요

  - not NULL 
  
  - `is_user_vaddr != NULL` / user 주소인가 
  
  - `pml4_get_page != NULL` / page table에 적절히 mapping된 주소인가

## System call

- system call의 동작에 따라 kernel 기능 활용하여 적절히 system call 구현

# Project 3: Virtual Memory

1. Memory Management

2. Anonymous Page

3. Stack Growth

4. Memory Mapped Files

5. Swap In / Out

## Memory Management

### Page Structure and Operations

- page 구조체 구현 완성

  - supplemental page table을 위한 elem / 기타 세부 속성 구현(accessible, writable)

- 각 page type별(`VM_UNINIT`, `VM_ANON`, `VM_FILE`) 함수 구현

- `vm.h`
```C
struct page_operations {
	bool (*swap_in) (struct page *, void *);
	bool (*swap_out) (struct page *);
	void (*destroy) (struct page *);
	enum vm_type type;
};
```
```C
#define swap_in(page, v) (page)->operations->swap_in ((page), v)
#define swap_out(page) (page)->operations->swap_out (page)
#define destroy(page) \
	if ((page)->operations->destroy) (page)->operations->destroy (page)
```

### Supplemental Page Table

- pintos page table

  - `pml4`: page table 추상화, 메모리에 로드되어있는 페이지만 저장

  - `SPT`: user program의 전체 page 저장

- Virtual Page와 physical frame을 효과적으로 관리하기 위해 supplemental page table 구현

  - list / hashmap...

- supplemental page table 기능 구현

  - `void supplemental_page_table_init`: SPT 초기화

  - `struct page *spt_find_page`: SPT에서 virtual address 검색

  - `bool spt_insert_page`: page를 SPT에 삽입

### Frame Management

- physical frame 관리를 위한 frame table 구현

  - `static struct frame *vm_get_frame`: 새로운 물리 페이지 할당 및 반환

  - `bool vm_do_claim_page`: `vm_get_frame`으로 frame 할당 후 page에 frame mapping / page_table에 저장

  - `boot vm_claim_page`: 해당 vm이 속한 page에 대해 `vm_do_claim_page` 호출

## Anonymous Page

### Lazy loading

#### uninit page lazy loading

- `load_segment` 함수에서 실행 파일의 segment를 메모리에 로드

  - `vm_alloc_page_with_initializer`로 초기화 / `lazy_load_segment`를 초기화 함수로 전달

  - 이후 page fault 발생시 `lazy_load_segment` 호출

#### page lazy loading

- lazy loading을 위한 `vm_alloc_page_with_initializer` 함수 구현

  - initializer를 호출하여 page 초기화, **memory load x**

  - 이후 page fault시 memory에 load (`vm_try_handle_fault`)

- page type(anon page, file page)별 initializer 구현

### Supplemental Page Table

- `supplemental_page_table_copy` 구현 → `fork`시 src table에서 dst table로 복사

  - 각 페이지 타입에 따른 처리 필요

- `supplemental_page_table_kill` 구현 → `process_exit`시 모든 page에 대해 적절한 `destructor` 호출

### Page Cleanup

- `uninit_destroy`, `anon_destry` 구현

## Stack Growth

- 동적인 user stack 확장 구현 → `vm_try_handle_fault()`

  - 적절한 page fault인지, 부적절한 page fault인지 확인
  
  - 적절한 page fault인 경우 `vm_stack_growth` 호출, 새 page 할당

## Memory Mapping File

- file을 memory에 mapping하는 `mmap`, `munmap` system call 구현

  - `mmap`: file descriptor가 가리키는 file을 memory mapping

  - `munmap`: `mmap`으로 할당된 mapping 제거

- `mmap` 구현

  - lazy loading

- `munmap` 구현

  - Unmapping 처리 → 변경된 page는 원본 파일에 기록 / 변경되지 않은 페이지는 바로 해제 (dirty flag)

- `file_backed_initializer`, `file_backed_destroy` 구현

## Swap In / Out

- `vm_anon_init`에서 swap disk 설정

- 

## Project 3 flow

### User program

- Program 실행 시 `initd`에서 SPT 초기화

- `process_exec` → `load` → `load_segment`에서 code 영역 memory 할당(lazy loading)

  - `vm_alloc_page_with_initializer`, `lazy_load_segment` 함수 전달하여 lazy load

- `setup_stack`에서 `vm_claim_page` 호출하여 user stack 할당

- 이후 메모리 접근시 `page_fault` → `vm_try_handle_fault`에서 page fault 처리

### Page Fault

- `vm_try_handle_fault`에서 page fault 처리

  - spt에 존재하지 않는 page의 경우 `vm_stack_growth` 호출하여 stack memory 확장

    - `vm_stack_growth` → `vm_alloc_page` → `vm_claim_page` → `vm_do_claim_page` → `pml4_set_page`

    - `vm_alloc_page`에서 유형별 page 구조체 setting

  - spt에 존재하는 page의 경우 `vm_do_claim_page` 호출하여 page를 메모리에 로드

### Fork

- `__do_fork` 함수에서 자식 thread의 spt 초기화 및 복사

  - `__do_fork` → `supplemental_page_table_copy`에서 spt 내 모든 page를 type별로 설정 및 데이터 복사


### `mmap`, `munmap`

- `do_mmap`에서 page 로드 및 file 데이터 로드

- `do_munmap`에서 `destroy`(`file_backed_destrpy`) 호출하여 dirty page를 파일에 기록, page 할당 해제

### process 종료시

- `process_exit` → `process_cleanup` 호출

- spt 할당 해제 및 `pml4_destroy` 호출하여 메모리 전체 해제