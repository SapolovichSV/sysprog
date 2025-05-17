#include "corobus.h"

#include "libcoro.h"
#include "rlist.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct data_vector
{
	unsigned *data;
	size_t size;
	size_t capacity;
};

#if 1 /* Uncomment this if want to use */

/** Append @a count messages in @a data to the end of the vector. */
static void
data_vector_append_many(struct data_vector *vector,
						const unsigned *data, size_t count)
{
	if (vector->size + count > vector->capacity)
	{
		if (vector->capacity == 0)
			vector->capacity = 4;
		else
			vector->capacity *= 2;
		if (vector->capacity < vector->size + count)
			vector->capacity = vector->size + count;
		vector->data = realloc(vector->data,
							   sizeof(vector->data[0]) * vector->capacity);
	}
	memcpy(&vector->data[vector->size], data, sizeof(data[0]) * count);
	vector->size += count;
}

/** Append a single message to the vector. */
static void
data_vector_append(struct data_vector *vector, unsigned data)
{
	data_vector_append_many(vector, &data, 1);
}

/** Pop @a count of messages into @a data from the head of the vector. */
static void
data_vector_pop_first_many(struct data_vector *vector, unsigned *data, size_t count)
{
	assert(count <= vector->size);
	memcpy(data, vector->data, sizeof(data[0]) * count);
	vector->size -= count;
	memmove(vector->data, &vector->data[count], vector->size * sizeof(vector->data[0]));
}

/** Pop a single message from the head of the vector. */
static unsigned
data_vector_pop_first(struct data_vector *vector)
{
	unsigned data = 0;
	data_vector_pop_first_many(vector, &data, 1);
	return data;
}

#endif

/**
 * One coroutine waiting to be woken up in a list of other
 * suspended coros.
 */
struct wakeup_entry
{
	struct rlist base;
	struct coro *coro;
};

/** A queue of suspended coros waiting to be woken up. */
struct wakeup_queue
{
	struct rlist coros;
};

#if 1 /* Uncomment this if want to use */

/** Suspend the current coroutine until it is woken up. */
static void
wakeup_queue_suspend_this(struct wakeup_queue *queue)
{
	struct wakeup_entry entry;
	entry.coro = coro_this();
	rlist_add_tail_entry(&queue->coros, &entry, base);
	coro_suspend();
	rlist_del_entry(&entry, base);
}

/** Wakeup the first coroutine in the queue. */
static void
wakeup_queue_wakeup_first(struct wakeup_queue *queue)
{
	if (rlist_empty(&queue->coros))
		return;
	struct wakeup_entry *entry = rlist_first_entry(&queue->coros,
												   struct wakeup_entry, base);
	coro_wakeup(entry->coro);
}

#endif

struct coro_bus_channel
{
	/** Channel max capacity. */
	size_t size_limit;
	/** Coroutines waiting until the channel is not full. */
	struct wakeup_queue send_queue;
	/** Coroutines waiting until the channel is not empty. */
	struct wakeup_queue recv_queue;
	/** Message queue. */
	struct data_vector data;
};

struct coro_bus
{
	struct coro_bus_channel **channels;
	int channel_count;
};

static enum coro_bus_error_code global_error = CORO_BUS_ERR_NONE;

enum coro_bus_error_code
coro_bus_errno(void)
{
	return global_error;
}

void coro_bus_errno_set(enum coro_bus_error_code err)
{
	global_error = err;
}

struct coro_bus *
coro_bus_new(void)
{
	struct coro_bus *bus = malloc(sizeof(struct coro_bus));
	bus->channel_count = 0;
	bus->channels = NULL;
	coro_bus_errno_set(CORO_BUS_ERR_NONE);
	return bus;
}

void coro_bus_delete(struct coro_bus *bus)
{
	if (bus == NULL)
		return;

	// Удаляем каждый канал, который ещё не был закрыт
	for (int i = 0; i < bus->channel_count; ++i)
	{
		struct coro_bus_channel *chan = bus->channels[i];
		if (chan)
		{
			// Удаляем буфер данных
			free(chan->data.data);
			// Очереди ожидания к этому моменту должны быть пустыми
			// (по контракту coro_bus_delete не вызывается, если есть
			// подвешенные корутины)
			free(chan);
		}
	}

	// Освобождаем массив указателей и сам bus
	free(bus->channels);
	free(bus);
}

int coro_bus_channel_open(struct coro_bus *bus, size_t size_limit)
{
	// 1) Сбросим ошибку
	coro_bus_errno_set(CORO_BUS_ERR_NONE);

	// 2) Попробуем найти старый свободный слот
	for (int i = 0; i < bus->channel_count; i++)
	{
		if (bus->channels[i] == NULL)
		{
			// Есть пустой слот — создаём канал и возвращаем его индекс
			struct coro_bus_channel *chan = malloc(sizeof(*chan));
			if (!chan)
			{
				coro_bus_errno_set(CORO_BUS_ERR_NO_CHANNEL);
				return -1;
			}
			chan->size_limit = size_limit;
			chan->data.data = malloc(sizeof(unsigned));
			chan->data.size = 0;
			chan->data.capacity = 0;
			rlist_create(&chan->recv_queue.coros);
			rlist_create(&chan->send_queue.coros);
			bus->channels[i] = chan;
			return i;
		}
	}

	// 3) Свободных слотов нет — расширяем массив
	int old_count = bus->channel_count;
	// Нужен новый буфер на old_count+1 указатель
	struct coro_bus_channel **new_arr =
		realloc(bus->channels, sizeof(*new_arr) * (old_count + 1));
	if (!new_arr)
	{
		coro_bus_errno_set(CORO_BUS_ERR_NO_CHANNEL);
		return -1;
	}
	bus->channels = new_arr;

	// 4) Создаём новый канал в конце
	struct coro_bus_channel *chan = malloc(sizeof(*chan));
	if (!chan)
	{
		coro_bus_errno_set(CORO_BUS_ERR_NO_CHANNEL);
		return -1;
	}
	chan->size_limit = size_limit;
	chan->data.data = malloc(sizeof(unsigned));
	chan->data.size = 0;
	chan->data.capacity = 0;
	rlist_create(&chan->recv_queue.coros);
	rlist_create(&chan->send_queue.coros);

	bus->channels[old_count] = chan;
	bus->channel_count = old_count + 1;

	return old_count;
}

void coro_bus_channel_close(struct coro_bus *bus, int channel)
{
	if (channel < 0 || channel >= bus->channel_count)
	{
		return;
	}
	struct coro_bus_channel *chan = bus->channels[channel];
	if (chan == NULL)
	{
		return;
	}

	// free(chan->data.data);
	// chan->data.size = 0;
	// chan->data.capacity = 0;
	// chan->data.data = NULL;
	coro_bus_errno_set(CORO_BUS_ERR_NO_CHANNEL);

	bus->channels[channel] = NULL;
	// waking up all
	while (!rlist_empty(&chan->recv_queue.coros))
	{
		wakeup_queue_wakeup_first(&chan->recv_queue);
		coro_yield();
	}
	while (!rlist_empty(&chan->send_queue.coros))
	{
		wakeup_queue_wakeup_first(&chan->send_queue);
		coro_yield();
	}

	free(chan->data.data);
	free(chan);
	return;
	// /* IMPLEMENT THIS FUNCTION */
	// (void)bus;
	// (void)channel;
	// /*
	//  * Be very attentive here. What happens, if the channel is
	//  * closed while there are coroutines waiting on it? For
	//  * example, the channel was empty, and some coros were
	//  * waiting on its recv_queue.
	//  *
	//  * If you wakeup those coroutines and just delete the
	//  * channel right away, then those waiting coroutines might
	//  * on wakeup try to reference invalid memory.
	//  *
	//  * Can happen, for example, if you use an intrusive list
	//  * (rlist), delete the list itself (by deleting the
	//  * channel), and then the coroutines on wakeup would try
	//  * to remove themselves from the already destroyed list.
	//  *
	//  * Think how you could address that. Remove all the
	//  * waiters from the list before freeing it? Yield this
	//  * coroutine after waking up the waiters but before
	//  * freeing the channel, so the waiters could safely leave?
	//  */
}

int coro_bus_send(struct coro_bus *bus, int channel, unsigned data)
{
	while (true)
	{
		int rc = coro_bus_try_send(bus, channel, data);
		if (rc == 0)
			return 0;

		enum coro_bus_error_code err = coro_bus_errno();
		if (err == CORO_BUS_ERR_NO_CHANNEL)
		{
			coro_bus_errno_set(CORO_BUS_ERR_NO_CHANNEL);
			return -1;
		}

		if (err == CORO_BUS_ERR_WOULD_BLOCK)
		{
			// Канал мог быть удалён — ещё раз проверим
			struct coro_bus_channel *chan = bus->channels[channel];
			if (chan == NULL)
			{
				coro_bus_errno_set(CORO_BUS_ERR_NO_CHANNEL);
				return -1;
			}

			// Уходим спать и продолжаем заново
			wakeup_queue_suspend_this(&chan->send_queue);

			// После пробуждения — цикл начнётся заново
		}
	}
	// /* IMPLEMENT THIS FUNCTION */
	// (void)bus;
	// (void)channel;
	// (void)data;

	// /*
	//  * Try sending in a loop, until success. If error, then
	//  * check which one is that. If 'wouldblock', then suspend
	//  * this coroutine and try again when woken up.
	//  *
	//  * If see the channel has space, then wakeup the first
	//  * coro in the send-queue. That is needed so when there is
	//  * enough space for many messages, and many coroutines are
	//  * waiting, they would then wake each other up one by one
	//  * as lone as there is still space.
	//  */
	// coro_bus_errno_set(CORO_BUS_ERR_NOT_IMPLEMENTED);
	// return -1;
}

int coro_bus_try_send(struct coro_bus *bus, int channel, unsigned data)
{
	if (channel < 0 || channel >= bus->channel_count || bus->channels[channel] == NULL)
	{
		coro_bus_errno_set(CORO_BUS_ERR_NO_CHANNEL);
		return -1;
	}
	struct coro_bus_channel *chan = bus->channels[channel];
	if (chan->data.size >= chan->size_limit)
	{
		coro_bus_errno_set(CORO_BUS_ERR_WOULD_BLOCK);
		return -1;
	}
	data_vector_append(&chan->data, data);
	wakeup_queue_wakeup_first(&chan->recv_queue);
	return 0;
}

int coro_bus_recv(struct coro_bus *bus, int channel, unsigned *data)
{
	while (true)
	{
		if (coro_bus_try_recv(bus, channel, data) == 0)
			return 0;
		if (coro_bus_errno() != CORO_BUS_ERR_WOULD_BLOCK)
			return -1;
		struct coro_bus_channel *chan = bus->channels[channel];
		if (chan == NULL)
		{
			coro_bus_errno_set(CORO_BUS_ERR_NO_CHANNEL);
			return -1;
		}
		if (coro_bus_errno() == CORO_BUS_ERR_NO_CHANNEL)
		{
			return -1;
		}
		wakeup_queue_suspend_this(&chan->recv_queue);
	}

	// /* IMPLEMENT THIS FUNCTION */
	// (void)bus;
	// (void)channel;
	// (void)data;
	// coro_bus_errno_set(CORO_BUS_ERR_NOT_IMPLEMENTED);
	// return -1;
}

int coro_bus_try_recv(struct coro_bus *bus, int channel, unsigned *data)
{
	if (channel < 0 || channel >= bus->channel_count || bus->channels[channel] == NULL)
	{
		coro_bus_errno_set(CORO_BUS_ERR_NO_CHANNEL);
		return -1;
	}
	struct coro_bus_channel *chan = bus->channels[channel];
	if (chan->data.size <= 0)
	{
		coro_bus_errno_set(CORO_BUS_ERR_WOULD_BLOCK);
		return -1;
	}

	*data = data_vector_pop_first(&chan->data);
	wakeup_queue_wakeup_first(&chan->send_queue);
	return 0;
	// /* IMPLEMENT THIS FUNCTION */
	// (void)bus;
	// (void)channel;
	// (void)data;
	// coro_bus_errno_set(CORO_BUS_ERR_NOT_IMPLEMENTED);
	// return -1;
}

#if NEED_BROADCAST

int coro_bus_broadcast(struct coro_bus *bus, unsigned data)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)bus;
	(void)data;
	coro_bus_errno_set(CORO_BUS_ERR_NOT_IMPLEMENTED);
	return -1;
}

int coro_bus_try_broadcast(struct coro_bus *bus, unsigned data)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)bus;
	(void)data;
	coro_bus_errno_set(CORO_BUS_ERR_NOT_IMPLEMENTED);
	return -1;
}

#endif

#if NEED_BATCH

int coro_bus_send_v(struct coro_bus *bus, int channel, const unsigned *data, unsigned count)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)bus;
	(void)channel;
	(void)data;
	(void)count;
	coro_bus_errno_set(CORO_BUS_ERR_NOT_IMPLEMENTED);
	return -1;
}

int coro_bus_try_send_v(struct coro_bus *bus, int channel, const unsigned *data, unsigned count)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)bus;
	(void)channel;
	(void)data;
	(void)count;
	coro_bus_errno_set(CORO_BUS_ERR_NOT_IMPLEMENTED);
	return -1;
}

int coro_bus_recv_v(struct coro_bus *bus, int channel, unsigned *data, unsigned capacity)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)bus;
	(void)channel;
	(void)data;
	(void)capacity;
	coro_bus_errno_set(CORO_BUS_ERR_NOT_IMPLEMENTED);
	return -1;
}

int coro_bus_try_recv_v(struct coro_bus *bus, int channel, unsigned *data, unsigned capacity)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)bus;
	(void)channel;
	(void)data;
	(void)capacity;
	coro_bus_errno_set(CORO_BUS_ERR_NOT_IMPLEMENTED);
	return -1;
}

#endif
