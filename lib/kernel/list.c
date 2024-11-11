#include "list.h"
#include "../debug.h"

/* 우리의 이중 연결 리스트는 두 개의 헤더 요소를 가지고 있습니다: 
   첫 번째 요소 바로 앞의 "head"와 마지막 요소 바로 뒤의 "tail".
   앞쪽 헤더의 `prev' 링크는 null이며, 뒤쪽 헤더의 `next' 링크도 null입니다.
   그들의 다른 두 링크는 리스트의 내부 요소를 통해 서로를 가리킵니다.

   빈 리스트는 다음과 같습니다:

   +------+     +------+
   <---| head |<--->| tail |--->
   +------+     +------+

   두 개의 요소가 있는 리스트는 다음과 같습니다:

   +------+     +-------+     +-------+     +------+
   <---| head |<--->|   1   |<--->|   2   |<--->| tail |<--->
   +------+     +-------+     +-------+     +------+

   이 배열의 대칭성은 리스트 처리에서 많은 특별한 경우를 제거합니다.
   예를 들어, list_remove()를 보십시오: 두 개의 포인터 할당만 필요하며 조건문이 없습니다.
   헤더 요소가 없었다면 코드가 훨씬 복잡했을 것입니다.

   (각 헤더 요소에서 사용되는 포인터가 하나뿐이므로, 
   이들을 단일 헤더 요소로 결합할 수 있지만, 
   두 개의 별도 요소를 사용하면 일부 작업에서 약간의 검사를 수행할 수 있어 유용할 수 있습니다.) */

static bool is_sorted (struct list_elem *a, struct list_elem *b,
		list_less_func *less, void *aux) UNUSED;

/* ELEM이 head이면 true를 반환하고, 그렇지 않으면 false를 반환합니다. */
static inline bool
is_head (struct list_elem *elem) {
	return elem != NULL && elem->prev == NULL && elem->next != NULL;
}

/* ELEM이 내부 요소이면 true를 반환하고, 그렇지 않으면 false를 반환합니다. */
static inline bool
is_interior (struct list_elem *elem) {
	return elem != NULL && elem->prev != NULL && elem->next != NULL;
}

/* ELEM이 tail이면 true를 반환하고, 그렇지 않으면 false를 반환합니다. */
static inline bool
is_tail (struct list_elem *elem) {
	return elem != NULL && elem->prev != NULL && elem->next == NULL;
}

/* LIST를 빈 리스트로 초기화합니다. */
void
list_init (struct list *list) {
	ASSERT (list != NULL);
	list->head.prev = NULL;
	list->head.next = &list->tail;
	list->tail.prev = &list->head;
	list->tail.next = NULL;
}

/* LIST의 시작을 반환합니다. */
struct list_elem *
list_begin (struct list *list) {
	ASSERT (list != NULL);
	return list->head.next;
}

/* ELEM의 리스트에서 다음 요소를 반환합니다. 
   ELEM이 리스트의 마지막 요소인 경우, 리스트의 tail을 반환합니다. 
   ELEM이 리스트의 tail인 경우 결과는 정의되지 않습니다. */
struct list_elem *
list_next (struct list_elem *elem) {
	ASSERT (is_head (elem) || is_interior (elem));
	return elem->next;
}

/* LIST의 tail을 반환합니다.

   list_end()는 리스트를 앞에서 뒤로 순회할 때 자주 사용됩니다. 
   list.h의 상단에 있는 큰 주석을 참조하십시오. */
struct list_elem *
list_end (struct list *list) {
	ASSERT (list != NULL);
	return &list->tail;
}

/* LIST의 역순 시작을 반환하여 LIST를 역순으로, 뒤에서 앞으로 순회할 수 있습니다. */
struct list_elem *
list_rbegin (struct list *list) {
	ASSERT (list != NULL);
	return list->tail.prev;
}

/* ELEM의 리스트에서 이전 요소를 반환합니다. 
   ELEM이 리스트의 첫 번째 요소인 경우, 리스트의 head를 반환합니다. 
   ELEM이 리스트의 head인 경우 결과는 정의되지 않습니다. */
struct list_elem *
list_prev (struct list_elem *elem) {
	ASSERT (is_interior (elem) || is_tail (elem));
	return elem->prev;
}

/* LIST의 head를 반환합니다.

   list_rend()는 LIST를 역순으로, 뒤에서 앞으로 순회할 때 자주 사용됩니다. 
   list.h의 상단에 있는 예제를 참조하십시오. 
   
	for (e = list_rbegin (&foo_list); e != list_rend (&foo_list); e = list_prev (e)) { 
		struct foo *f = list_entry (e, struct foo, elem); 
		...do something with f... 
	} 
*/
struct list_elem *
list_rend (struct list *list) {
	ASSERT (list != NULL);
	return &list->head;
}

/* LIST의 head를 반환합니다.

   list_head()는 리스트를 순회하는 대체 스타일로 사용할 수 있습니다. 예를 들어:

   e = list_head (&list);
   while ((e = list_next (e)) != list_end (&list))
   {
   ...
   }
   */
struct list_elem *
list_head (struct list *list) {
	ASSERT (list != NULL);
	return &list->head;
}

/* LIST의 tail을 반환합니다. */
struct list_elem *
list_tail (struct list *list) {
	ASSERT (list != NULL);
	return &list->tail;
}

/* ELEM을 BEFORE 바로 앞에 삽입합니다. BEFORE는 내부 요소이거나 tail일 수 있습니다. 
   후자의 경우는 list_push_back()과 동일합니다. */
void
list_insert (struct list_elem *before, struct list_elem *elem) {
	ASSERT (is_interior (before) || is_tail (before));
	ASSERT (elem != NULL);

	elem->prev = before->prev;
	elem->next = before;
	before->prev->next = elem;
	before->prev = elem;
}

/* FIRST부터 LAST(포함하지 않음)까지의 요소를 현재 리스트에서 제거한 후, 
   BEFORE 바로 앞에 삽입합니다. BEFORE는 내부 요소이거나 tail일 수 있습니다. */
void
list_splice (struct list_elem *before,
		struct list_elem *first, struct list_elem *last) {
	ASSERT (is_interior (before) || is_tail (before));
	if (first == last)
		return;
	last = list_prev (last);

	ASSERT (is_interior (first));
	ASSERT (is_interior (last));

	/* FIRST...LAST를 현재 리스트에서 깔끔하게 제거합니다. */
	first->prev->next = last->next;
	last->next->prev = first->prev;

	/* FIRST...LAST를 새로운 리스트에 연결합니다. */
	first->prev = before->prev;
	last->next = before;
	before->prev->next = first;
	before->prev = last;
}

/* ELEM을 LIST의 시작에 삽입하여 LIST의 앞쪽이 되도록 합니다. */
void
list_push_front (struct list *list, struct list_elem *elem) {
	list_insert (list_begin (list), elem);
}

/* ELEM을 LIST의 끝에 삽입하여 LIST의 뒤쪽이 되도록 합니다. */
void
list_push_back (struct list *list, struct list_elem *elem) {
	list_insert (list_end (list), elem);
}

/* ELEM을 리스트에서 제거하고 그 뒤에 있는 요소를 반환합니다. 
   ELEM이 리스트에 없는 경우 정의되지 않은 동작을 합니다.

   ELEM을 제거한 후 리스트의 요소로 취급하는 것은 안전하지 않습니다. 
   특히, 제거 후 ELEM에 대해 list_next()나 list_prev()를 사용하는 것은 
   정의되지 않은 동작을 초래합니다. 이는 리스트의 요소를 제거하는 
   순진한 루프가 실패할 수 있음을 의미합니다:

 ** 이렇게 하지 마세요 **
 for (e = list_begin (&list); e != list_end (&list); e = list_next (e))
 {
 ...e로 무언가를 합니다...
 list_remove (e);
 }
 ** 이렇게 하지 마세요 **

 리스트의 요소를 순회하고 제거하는 올바른 방법 중 하나는 다음과 같습니다:

for (e = list_begin (&list); e != list_end (&list); e = list_remove (e))
{
...e로 무언가를 합니다...
}

리스트의 요소를 free()해야 하는 경우 더 보수적으로 접근해야 합니다. 
이 경우에도 작동하는 대체 전략은 다음과 같습니다:

while (!list_empty (&list))
{
struct list_elem *e = list_pop_front (&list);
...e로 무언가를 합니다...
}
*/
struct list_elem *
list_remove (struct list_elem *elem) {
	ASSERT (is_interior (elem));
	elem->prev->next = elem->next;
	elem->next->prev = elem->prev;
	return elem->next;
}

/* LIST의 앞쪽 요소를 제거하고 반환합니다.
   LIST가 제거 전 비어 있는 경우 정의되지 않은 동작을 합니다. */
struct list_elem *
list_pop_front (struct list *list) {
	struct list_elem *front = list_front (list);
	list_remove (front);
	return front;
}

/* LIST의 뒤쪽 요소를 제거하고 반환합니다.
   LIST가 제거 전 비어 있는 경우 정의되지 않은 동작을 합니다. */
struct list_elem *
list_pop_back (struct list *list) {
	struct list_elem *back = list_back (list);
	list_remove (back);
	return back;
}

/* LIST의 앞쪽 요소를 반환합니다.
   LIST가 비어 있는 경우 정의되지 않은 동작을 합니다. */
struct list_elem *
list_front (struct list *list) {
	ASSERT (!list_empty (list));
	return list->head.next;
}

/* LIST의 뒤쪽 요소를 반환합니다.
   LIST가 비어 있는 경우 정의되지 않은 동작을 합니다. */
struct list_elem *
list_back (struct list *list) {
	ASSERT (!list_empty (list));
	return list->tail.prev;
}

/* LIST의 요소 수를 반환합니다.
   LIST의 요소 수에 대해 O(n) 시간이 소요됩니다. */
size_t
list_size (struct list *list) {
	struct list_elem *e;
	size_t cnt = 0;

	for (e = list_begin (list); e != list_end (list); e = list_next (e))
		cnt++;
	return cnt;
}

/* LIST가 비어 있으면 true를 반환하고, 그렇지 않으면 false를 반환합니다. */
bool
list_empty (struct list *list) {
	return list_begin (list) == list_end (list);
}

/* A와 B가 가리키는 `struct list_elem *'를 교환합니다. */
static void
swap (struct list_elem **a, struct list_elem **b) {
	struct list_elem *t = *a;
	*a = *b;
	*b = t;
}

/* LIST의 순서를 반대로 합니다. */
void
list_reverse (struct list *list) {
	if (!list_empty (list)) {
		struct list_elem *e;

		for (e = list_begin (list); e != list_end (list); e = e->prev)
			swap (&e->prev, &e->next);
		swap (&list->head.next, &list->tail.prev);
		swap (&list->head.next->prev, &list->tail.prev->next);
	}
}

/* 리스트 요소 A부터 B(포함하지 않음)까지가 주어진 보조 데이터 AUX를 사용하는 
   LESS에 따라 정렬되어 있는 경우에만 true를 반환합니다. */
static bool
is_sorted (struct list_elem *a, struct list_elem *b,
		list_less_func *less, void *aux) {
	if (a != b)
		while ((a = list_next (a)) != b)
			if (less (a, list_prev (a), aux))
				return false;
	return true;
}

/* A부터 B(포함하지 않음)까지의 리스트 요소가 LESS에 따라 
   비감소 순서로 정렬된 실행을 찾습니다. 
   실행의 (포함하지 않는) 끝을 반환합니다.
   A부터 B(포함하지 않음)까지는 비어 있지 않은 범위를 형성해야 합니다. */
static struct list_elem *
find_end_of_run (struct list_elem *a, struct list_elem *b,
		list_less_func *less, void *aux) {
	ASSERT (a != NULL);
	ASSERT (b != NULL);
	ASSERT (less != NULL);
	ASSERT (a != b);

	do {
		a = list_next (a);
	} while (a != b && !less (a, list_prev (a), aux));
	return a;
}

/* A0부터 A1B0(포함하지 않음)까지와 A1B0부터 B1(포함하지 않음)까지를 병합하여 
   B1(포함하지 않음)까지의 결합된 범위를 형성합니다. 
   두 입력 범위는 비어 있지 않고 주어진 보조 데이터 AUX를 사용하는 
   LESS에 따라 비감소 순서로 정렬되어 있어야 합니다. 
   출력 범위도 동일한 방식으로 정렬됩니다. */
static void
inplace_merge (struct list_elem *a0, struct list_elem *a1b0,
		struct list_elem *b1,
		list_less_func *less, void *aux) {
	ASSERT (a0 != NULL);
	ASSERT (a1b0 != NULL);
	ASSERT (b1 != NULL);
	ASSERT (less != NULL);
	ASSERT (is_sorted (a0, a1b0, less, aux));
	ASSERT (is_sorted (a1b0, b1, less, aux));

	while (a0 != a1b0 && a1b0 != b1)
		if (!less (a1b0, a0, aux))
			a0 = list_next (a0);
		else {
			a1b0 = list_next (a1b0);
			list_splice (a0, list_prev (a1b0), a1b0);
		}
}

/* LIST를 주어진 보조 데이터 AUX를 사용하는 LESS에 따라 정렬합니다. 
   LIST의 요소 수에 대해 O(n lg n) 시간과 O(1) 공간을 사용하는 
   자연 반복 병합 정렬을 사용합니다. */
void
list_sort (struct list *list, list_less_func *less, void *aux) {
	size_t output_run_cnt;        /* 현재 패스에서 출력된 실행 수. */

	ASSERT (list != NULL);
	ASSERT (less != NULL);

	/* 리스트를 반복적으로 통과하여 인접한 비감소 요소의 실행을 병합합니다. 
	   하나의 실행만 남을 때까지 반복합니다. */
	do {
		struct list_elem *a0;     /* 첫 번째 실행의 시작. */
		struct list_elem *a1b0;   /* 첫 번째 실행의 끝, 두 번째 실행의 시작. */
		struct list_elem *b1;     /* 두 번째 실행의 끝. */

		output_run_cnt = 0;
		for (a0 = list_begin (list); a0 != list_end (list); a0 = b1) {
			/* 각 반복은 하나의 출력 실행을 생성합니다. */
			output_run_cnt++;

			/* 인접한 비감소 요소의 두 실행 A0...A1B0과 A1B0...B1을 찾습니다. */
			a1b0 = find_end_of_run (a0, list_end (list), less, aux);
			if (a1b0 == list_end (list))
				break;
			b1 = find_end_of_run (a1b0, list_end (list), less, aux);

			/* 실행을 병합합니다. */
			inplace_merge (a0, a1b0, b1, less, aux);
		}
	}
	while (output_run_cnt > 1);

	ASSERT (is_sorted (list_begin (list), list_end (list), less, aux));
}

/* ELEM을 LIST의 적절한 위치에 삽입합니다. LIST는 AUX를 보조 데이터로 사용하는
   LESS에 따라 정렬되어 있어야 합니다.
   LIST의 요소 수에 대해 평균적으로 O(n)의 시간이 소요됩니다. */
void
list_insert_ordered (struct list *list, struct list_elem *elem,
		list_less_func *less, void *aux) {
	struct list_elem *e;

	ASSERT (list != NULL);
	ASSERT (elem != NULL);
	ASSERT (less != NULL);

	for (e = list_begin (list); e != list_end (list); e = list_next (e))
		if (less (elem, e, aux))
			break;
	return list_insert (e, elem);
}

/* LIST를 순회하며 LESS에 따라 인접한 요소 중 첫 번째 요소를 제외한 모든 요소를 제거합니다.
   DUPLICATES가 null이 아닌 경우, LIST에서 제거된 요소를 DUPLICATES에 추가합니다. */
void
list_unique (struct list *list, struct list *duplicates,
		list_less_func *less, void *aux) {
	struct list_elem *elem, *next;

	ASSERT (list != NULL);
	ASSERT (less != NULL);
	if (list_empty (list))
		return;

	elem = list_begin (list);
	while ((next = list_next (elem)) != list_end (list))
		if (!less (elem, next, aux) && !less (next, elem, aux)) {
			list_remove (next);
			if (duplicates != NULL)
				list_push_back (duplicates, next);
		} else
			elem = next;
}

/* LIST에서 LESS에 따라 가장 큰 값을 가진 요소를 반환합니다. 
   최대값이 여러 개 있는 경우, 리스트에서 더 일찍 나타나는 것을 반환합니다. 
   리스트가 비어 있는 경우, tail을 반환합니다. */
struct list_elem *
list_max (struct list *list, list_less_func *less, void *aux) {
	struct list_elem *max = list_begin (list);
	if (max != list_end (list)) {
		struct list_elem *e;

		for (e = list_next (max); e != list_end (list); e = list_next (e))
			if (less (max, e, aux))
				max = e;
	}
	return max;
}

/* LIST에서 LESS에 따라 가장 작은 값을 가진 요소를 반환합니다. 
   최소값이 여러 개 있는 경우, 리스트에서 더 일찍 나타나는 것을 반환합니다. 
   리스트가 비어 있는 경우, tail을 반환합니다. */
struct list_elem *
list_min (struct list *list, list_less_func *less, void *aux) {
	struct list_elem *min = list_begin (list);
	if (min != list_end (list)) {
		struct list_elem *e;

		for (e = list_next (min); e != list_end (list); e = list_next (e))
			if (less (e, min, aux))
				min = e;
	}
	return min;
}