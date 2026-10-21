// SPDX-License-Identifier: GPL-2.0
/* Checked software admission and module/device lifetime, not electrical PM. */
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include "apu_checked_power.h"

struct apu_checked_owner {
	struct kref ref;
	struct mutex operation_lock;
	struct mutex lock;
	struct list_head clients;
	struct task_struct *operation_task;
	struct device *device;
	struct module *module;
	struct apu_checked_backend backend;
	enum apu_checked_state state;
	unsigned int client_count;
	unsigned int lease_count;
	int error;
	bool provider_pinned;
	bool stopping_admission;
	bool closed;
};

struct apu_checked_client {
	struct kref ref;
	struct list_head node;
	struct apu_checked_owner *owner;
	struct device *device;
	struct module *module;
	struct apu_checked_client_ops ops;
	void *ctx;
	unsigned int id;
	unsigned int lease_count;
	bool registered;
	bool module_pinned;
	bool uncertain;
};

struct apu_checked_lease {
	struct kref ref;
	struct apu_checked_client *client;
	bool released;
};

static int checked_errno(int error)
{
	return error > 0 ? -EPROTO : error;
}

static void owner_release(struct kref *ref)
{
	struct apu_checked_owner *owner =
		container_of(ref, struct apu_checked_owner, ref);

	put_device(owner->device);
	kfree(owner);
}

static void client_release(struct kref *ref)
{
	struct apu_checked_client *client =
		container_of(ref, struct apu_checked_client, ref);

	put_device(client->device);
	kref_put(&client->owner->ref, owner_release);
	kfree(client);
}

static void lease_release(struct kref *ref)
{
	struct apu_checked_lease *lease =
		container_of(ref, struct apu_checked_lease, ref);

	apu_checked_client_unref(lease->client);
	kfree(lease);
}

/* The operation lock spans callbacks; the cache/registry lock never does. */
static int operation_begin(struct apu_checked_owner *owner, bool admission)
{
	int ret = 0;

	mutex_lock(&owner->lock);
	if (owner->operation_task == current)
		ret = -EDEADLK;
	else if (owner->closed || (admission && owner->stopping_admission))
		ret = -ESHUTDOWN;
	mutex_unlock(&owner->lock);
	if (ret)
		return ret;
	mutex_lock(&owner->operation_lock);
	mutex_lock(&owner->lock);
	if (owner->closed || (admission && owner->stopping_admission))
		ret = -ESHUTDOWN;
	else
		owner->operation_task = current;
	mutex_unlock(&owner->lock);
	if (ret)
		mutex_unlock(&owner->operation_lock);
	return ret;
}

static void operation_end(struct apu_checked_owner *owner)
{
	mutex_lock(&owner->lock);
	owner->operation_task = NULL;
	mutex_unlock(&owner->lock);
	mutex_unlock(&owner->operation_lock);
}

/* operation_lock is held, so backend and all state commits are serialized. */
static int cached_fault(struct apu_checked_owner *owner)
{
	bool temporary = !owner->provider_pinned;
	int ret;

	if (temporary) {
		if (!try_module_get(owner->module))
			return -ENODEV;
		owner->provider_pinned = true;
	}
	ret = checked_errno(owner->backend.fault(owner->backend.ctx));
	if (!ret && temporary) {
		owner->provider_pinned = false;
		module_put(owner->module);
	}
	return ret;
}

static int quarantine(struct apu_checked_owner *owner, int error)
{
	/* Even a fault discovered before admission must not unpin its provider. */
	if (!owner->provider_pinned && try_module_get(owner->module))
		owner->provider_pinned = true;
	mutex_lock(&owner->lock);
	if (!owner->error)
		owner->error = checked_errno(error) ?: -EIO;
	owner->state = APU_CHECKED_FAILED;
	error = owner->error;
	mutex_unlock(&owner->lock);
	return error;
}

static int admitted(struct apu_checked_owner *owner)
{
	int ret;

	mutex_lock(&owner->lock);
	ret = owner->state == APU_CHECKED_FAILED ? owner->error : 0;
	mutex_unlock(&owner->lock);
	if (!ret) {
		ret = cached_fault(owner);
		if (ret && owner->provider_pinned)
			ret = quarantine(owner, ret);
	}
	return ret;
}

int apu_checked_owner_create(struct device *device, struct module *module,
	const struct apu_checked_backend *backend, struct apu_checked_owner **out)
{
	struct apu_checked_owner *owner;

	if (!device || !backend || !backend->on || !backend->off ||
	    !backend->fault || !out)
		return -EINVAL;
	owner = kzalloc(sizeof(*owner), GFP_KERNEL);
	if (!owner)
		return -ENOMEM;
	kref_init(&owner->ref);
	mutex_init(&owner->operation_lock);
	mutex_init(&owner->lock);
	INIT_LIST_HEAD(&owner->clients);
	owner->device = get_device(device);
	owner->module = module;
	owner->backend = *backend;
	owner->state = APU_CHECKED_OFF;
	*out = owner;
	return 0;
}

struct apu_checked_owner *apu_checked_owner_ref(struct apu_checked_owner *owner)
{
	if (owner)
		kref_get(&owner->ref);
	return owner;
}

void apu_checked_owner_unref(struct apu_checked_owner *owner)
{
	if (owner)
		kref_put(&owner->ref, owner_release);
}

int apu_checked_owner_stop(struct apu_checked_owner *owner)
{
	int ret = 0;

	if (!owner)
		return -EINVAL;
	mutex_lock(&owner->lock);
	if (owner->operation_task == current)
		ret = -EDEADLK;
	else if (owner->closed)
		ret = -ESHUTDOWN;
	else
		owner->stopping_admission = true;
	mutex_unlock(&owner->lock);
	if (ret)
		return ret;
	/* Admission is closed before waiting, including already queued gets. */
	ret = operation_begin(owner, false);
	if (!ret)
		operation_end(owner);
	return ret;
}

int apu_checked_owner_destroy(struct apu_checked_owner *owner)
{
	int ret;

	if (!owner)
		return -EINVAL;
	ret = operation_begin(owner, false);
	if (ret)
		return ret;
	mutex_lock(&owner->lock);
	if (owner->state != APU_CHECKED_OFF || owner->client_count ||
	    owner->lease_count || owner->provider_pinned)
		ret = -EBUSY;
	else
		owner->closed = true;
	mutex_unlock(&owner->lock);
	operation_end(owner);
	if (!ret)
		kref_put(&owner->ref, owner_release);
	return ret;
}

int apu_checked_owner_query(struct apu_checked_owner *owner,
	struct apu_checked_snapshot *out)
{
	if (!owner || !out)
		return -EINVAL;
	mutex_lock(&owner->lock);
	*out = (struct apu_checked_snapshot) {
		.state = owner->state,
		.error = owner->error,
		.leases = owner->lease_count,
		.clients = owner->client_count,
		.transitioning = owner->operation_task != NULL,
		.accepting = !owner->closed && !owner->stopping_admission,
	};
	mutex_unlock(&owner->lock);
	return 0;
}

struct apu_checked_client *apu_checked_client_ref(struct apu_checked_client *client)
{
	if (client)
		kref_get(&client->ref);
	return client;
}

void apu_checked_client_unref(struct apu_checked_client *client)
{
	if (client)
		kref_put(&client->ref, client_release);
}

int apu_checked_client_register(struct apu_checked_owner *owner,
	unsigned int id, struct device *device, struct module *module,
	const struct apu_checked_client_ops *ops, void *ctx,
	struct apu_checked_client **out)
{
	struct apu_checked_client *client, *iter;
	int ret;

	if (!owner || !device || !out)
		return -EINVAL;
	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return -ENOMEM;
	ret = operation_begin(owner, true);
	if (ret)
		goto free;
	ret = admitted(owner);
	if (ret)
		goto end;
	mutex_lock(&owner->lock);
	if (owner->client_count == ~0U) {
		ret = -EOVERFLOW;
		goto unlock;
	}
	list_for_each_entry(iter, &owner->clients, node) {
		if (iter->id == id) {
			ret = -EEXIST;
			goto unlock;
		}
	}
	kref_init(&client->ref);
	kref_get(&client->ref); /* Registry reference, independent of caller. */
	kref_get(&owner->ref);
	client->owner = owner;
	client->device = get_device(device);
	client->module = module;
	if (ops)
		client->ops = *ops;
	client->ctx = ctx;
	client->id = id;
	client->registered = true;
	list_add_tail(&client->node, &owner->clients);
	owner->client_count++;
	*out = client;
unlock:
	mutex_unlock(&owner->lock);
end:
	operation_end(owner);
free:
	if (ret)
		kfree(client);
	return ret;
}

int apu_checked_client_close(struct apu_checked_client *client)
{
	struct apu_checked_owner *owner;
	bool remove = false;
	int ret;

	if (!client)
		return -EINVAL;
	owner = client->owner;
	ret = operation_begin(owner, false);
	if (ret)
		return ret;
	mutex_lock(&owner->lock);
	if (!client->registered)
		ret = -EALREADY;
	else if (client->lease_count || client->uncertain || client->module_pinned)
		ret = -EBUSY;
	else {
		client->registered = false;
		list_del(&client->node);
		owner->client_count--;
		remove = true;
	}
	mutex_unlock(&owner->lock);
	operation_end(owner);
	if (remove)
		apu_checked_client_unref(client);
	return ret;
}

int apu_checked_client_query(struct apu_checked_client *client,
	struct apu_checked_client_snapshot *out)
{
	struct apu_checked_owner *owner;

	if (!client || !out)
		return -EINVAL;
	owner = client->owner;
	mutex_lock(&owner->lock);
	*out = (struct apu_checked_client_snapshot) {
		.owner_state = owner->state,
		.error = owner->error,
		.leases = client->lease_count,
		.registered = client->registered,
		.uncertain = client->uncertain,
	};
	mutex_unlock(&owner->lock);
	return 0;
}

int apu_checked_owner_query_id(struct apu_checked_owner *owner,
	unsigned int id, struct apu_checked_client_snapshot *out)
{
	struct apu_checked_client *client;
	int ret = -ENOENT;

	if (!owner || !out)
		return -EINVAL;
	mutex_lock(&owner->lock);
	list_for_each_entry(client, &owner->clients, node) {
		if (client->id != id)
			continue;
		*out = (struct apu_checked_client_snapshot) {
			.owner_state = owner->state,
			.error = owner->error,
			.leases = client->lease_count,
			.registered = client->registered,
			.uncertain = client->uncertain,
		};
		ret = 0;
		break;
	}
	mutex_unlock(&owner->lock);
	return ret;
}

int apu_checked_get(struct apu_checked_client *client,
	struct apu_checked_lease **out)
{
	struct apu_checked_owner *owner;
	struct apu_checked_lease *lease;
	bool first, active, new_provider = false;
	int ret;

	if (!client || !out)
		return -EINVAL;
	lease = kzalloc(sizeof(*lease), GFP_KERNEL);
	if (!lease)
		return -ENOMEM;
	owner = client->owner;
	ret = operation_begin(owner, true);
	if (ret)
		goto free;
	ret = admitted(owner);
	if (ret)
		goto end;
	mutex_lock(&owner->lock);
	if (!client->registered)
		ret = -ESHUTDOWN;
	else if (client->lease_count == ~0U || owner->lease_count == ~0U)
		ret = -EOVERFLOW;
	first = owner->lease_count == 0;
	active = client->lease_count != 0;
	mutex_unlock(&owner->lock);
	if (ret)
		goto end;
	if (active)
		goto commit;
	/* Pin before entering callback/backend code; no registration self-pin. */
	if (!owner->provider_pinned) {
		if (!try_module_get(owner->module)) {
			ret = -ENODEV;
			goto end;
		}
		owner->provider_pinned = true;
		new_provider = true;
	}
	if (!try_module_get(client->module)) {
		if (new_provider) {
			owner->provider_pinned = false;
			module_put(owner->module);
		}
		ret = -ENODEV;
		goto end;
	}
	client->module_pinned = true;
	mutex_lock(&owner->lock);
	owner->state = APU_CHECKED_STARTING;
	mutex_unlock(&owner->lock);
	ret = checked_errno(owner->backend.on(owner->backend.ctx, client->id, first));
	if (!ret)
		ret = cached_fault(owner);
	if (!ret && client->ops.on_init)
		ret = checked_errno(client->ops.on_init(client->ctx));
	if (!ret)
		ret = cached_fault(owner);
	if (ret) {
		mutex_lock(&owner->lock);
		client->uncertain = true;
		mutex_unlock(&owner->lock);
		ret = quarantine(owner, ret);
		goto end;
	}
commit:
	kref_init(&lease->ref);
	lease->client = apu_checked_client_ref(client);
	mutex_lock(&owner->lock);
	client->lease_count++;
	owner->lease_count++;
	owner->state = APU_CHECKED_ON;
	mutex_unlock(&owner->lock);
	*out = lease;
end:
	operation_end(owner);
free:
	if (ret)
		kfree(lease);
	return ret;
}

struct apu_checked_lease *apu_checked_lease_ref(struct apu_checked_lease *lease)
{
	if (lease)
		kref_get(&lease->ref);
	return lease;
}

void apu_checked_lease_unref(struct apu_checked_lease *lease)
{
	if (!lease)
		return;
	/* An active or uncertain lease must remain reachable by its owner. */
	if (WARN_ON_ONCE(!READ_ONCE(lease->released)))
		return;
	kref_put(&lease->ref, lease_release);
}

int apu_checked_put(struct apu_checked_lease *lease)
{
	struct apu_checked_client *client;
	struct apu_checked_owner *owner;
	bool last, shared;
	int ret;

	if (!lease)
		return -EINVAL;
	client = lease->client;
	owner = client->owner;
	ret = operation_begin(owner, false);
	if (ret)
		return ret;
	ret = admitted(owner);
	if (ret)
		goto end;
	mutex_lock(&owner->lock);
	if (lease->released || !client->lease_count)
		ret = -EALREADY;
	last = owner->lease_count == 1;
	shared = client->lease_count > 1;
	mutex_unlock(&owner->lock);
	if (ret)
		goto end;
	if (shared) {
		mutex_lock(&owner->lock);
		client->lease_count--;
		owner->lease_count--;
		WRITE_ONCE(lease->released, true);
		mutex_unlock(&owner->lock);
		goto end;
	}
	if (client->ops.pre_off) {
		int fault;

		ret = checked_errno(client->ops.pre_off(client->ctx));
		if (ret && ret != -EAGAIN && ret != -EBUSY) {
			ret = quarantine(owner, ret);
			goto end;
		}
		fault = cached_fault(owner);
		if (fault) {
			ret = quarantine(owner, fault);
			goto end;
		}
		if (ret == -EAGAIN || ret == -EBUSY)
			goto end; /* Contract: inspection has changed no state. */
	}
	mutex_lock(&owner->lock);
	owner->state = APU_CHECKED_STOPPING;
	mutex_unlock(&owner->lock);
	if (client->ops.quiesce)
		ret = checked_errno(client->ops.quiesce(client->ctx));
	if (!ret)
		ret = cached_fault(owner);
	if (!ret)
		ret = checked_errno(owner->backend.off(owner->backend.ctx, client->id, last));
	if (!ret)
		ret = cached_fault(owner);
	if (ret) {
		mutex_lock(&owner->lock);
		client->uncertain = true;
		mutex_unlock(&owner->lock);
		ret = quarantine(owner, ret);
		goto end;
	}
	mutex_lock(&owner->lock);
	client->lease_count--;
	WRITE_ONCE(lease->released, true);
	owner->lease_count--;
	owner->state = last ? APU_CHECKED_OFF : APU_CHECKED_ON;
	mutex_unlock(&owner->lock);
	client->module_pinned = false;
	module_put(client->module);
	if (last) {
		owner->provider_pinned = false;
		module_put(owner->module);
	}
end:
	operation_end(owner);
	return ret;
}

int apu_checked_run(struct apu_checked_owner *owner,
	int (*fn)(void *arg), void *arg)
{
	int ret, fault;

	if (!owner || !fn)
		return -EINVAL;
	ret = operation_begin(owner, true);
	if (ret)
		return ret;
	ret = admitted(owner);
	if (ret)
		goto end;
	mutex_lock(&owner->lock);
	if (owner->state != APU_CHECKED_ON)
		ret = -EHOSTDOWN;
	mutex_unlock(&owner->lock);
	if (ret)
		goto end;
	ret = checked_errno(fn(arg));
	fault = cached_fault(owner);
	if (fault)
		ret = quarantine(owner, fault);
end:
	operation_end(owner);
	return ret;
}
