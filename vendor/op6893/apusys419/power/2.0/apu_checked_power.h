/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __APU_CHECKED_POWER_H__
#define __APU_CHECKED_POWER_H__

#include <linux/types.h>

struct device;
struct module;
struct apu_checked_owner;
struct apu_checked_client;
struct apu_checked_lease;

enum apu_checked_state {
	APU_CHECKED_OFF,
	APU_CHECKED_STARTING,
	APU_CHECKED_ON,
	APU_CHECKED_STOPPING,
	APU_CHECKED_FAILED,
};

/* Published software phase/counts, never proof of physical power or access. */
struct apu_checked_snapshot {
	enum apu_checked_state state;
	int error;
	unsigned int leases;
	unsigned int clients;
	bool transitioning;
	bool accepting;
};

struct apu_checked_client_snapshot {
	enum apu_checked_state owner_state;
	int error;
	unsigned int leases;
	bool registered;
	bool uncertain;
};

/*
 * on/off are the only electrical transition entry points. Any error is an
 * uncertain transition: no guessed rollback, no lease/count commit, and the
 * first errno and all acquired module references are retained. first/last
 * describe the shared owner, while id selects the client's engine.
 * fault must return a cached errno, without MMIO or another owner operation.
 * The owner copies these operations; ctx must outlive successful destroy.
 */
struct apu_checked_backend {
	int (*on)(void *ctx, unsigned int id, bool first);
	int (*off)(void *ctx, unsigned int id, bool last);
	int (*fault)(void *ctx);
	void *ctx;
};

/*
 * These operations belong to this client, not a global callback registry.
 * on_init runs after electrical on. pre_off must be entirely side-effect-free
 * inspection: -EAGAIN/-EBUSY veto and preserve the lease; any other failure
 * quarantines the owner. quiesce may change hardware/firmware and every error
 * quarantines it. No callback runs under the registry/cache mutex.
 * ctx must outlive successful close. The operations are copied by register.
 */
struct apu_checked_client_ops {
	int (*on_init)(void *ctx);
	int (*pre_off)(void *ctx);
	int (*quiesce)(void *ctx);
};

/*
 * All APIs sleep. Every caller must separately protect the provider's code for
 * the complete API entry, normally by a static imported-symbol dependency.
 * Active module pins protect ownership, not execution after the final put.
 * The owner handle returned by create is caller-owned;
 * successful destroy consumes it. Temporary owner_ref references must be
 * obtained from an existing reference or under the caller's publication lock.
 * The caller must stop publishing new entries before destroy; already admitted
 * callers retain their references, drain, and see -ESHUTDOWN after close.
 * Cached queries remain valid until their final reference is released.
 * A failed destroy must not free backend ctx or the provider's resources.
 * Register returns a caller-owned client reference in addition to the registry
 * reference. close drains operations and removes the registry reference only
 * on success; it does not consume the caller reference. The caller must retain
 * a client reference throughout every API entry. client_ref is safe ONLY when
 * already holding a reference or the caller's publication lock protects it;
 * it cannot resurrect an unprotected raw pointer racing its final unref.
 * A unique id may register once. Additional gets of an active client create
 * leases without another electrical on; only its last put runs off phases.
 * Get returns a caller-owned lease only after all on phases succeed. Put does
 * not consume the lease handle: it marks it released on success; lease_unref
 * must follow. A lease holds its client/owner alive. Failed put retains the
 * active lease and references; the caller must not discard its ownership.
 * Every lease reference, including lease_ref clones, may be dropped only after
 * successful put. Active lease_unref warns and retains that reference.
 *
 * A device reference protects allocation, not devm resources against unbind.
 * Drivers must prevent unbind/removal while active/uncertain. Module references
 * are acquired for active transitions/leases, never merely by registration;
 * inactive clients can close during module_exit. FAILED intentionally pins
 * the provider and affected clients until a future verified recovery path.
 * Clock/regulator/DMA ownership and suspend/resume are not implemented here.
 */
int apu_checked_owner_create(struct device *device, struct module *module,
	const struct apu_checked_backend *backend,
	struct apu_checked_owner **out);
struct apu_checked_owner *apu_checked_owner_ref(struct apu_checked_owner *owner);
void apu_checked_owner_unref(struct apu_checked_owner *owner);
/* Permanently stop new get/register/run and drain current callbacks. */
int apu_checked_owner_stop(struct apu_checked_owner *owner);
int apu_checked_owner_destroy(struct apu_checked_owner *owner);
int apu_checked_owner_query(struct apu_checked_owner *owner,
	struct apu_checked_snapshot *out);
int apu_checked_client_register(struct apu_checked_owner *owner,
	unsigned int id, struct device *device, struct module *module,
	const struct apu_checked_client_ops *ops, void *ctx,
	struct apu_checked_client **out);
struct apu_checked_client *apu_checked_client_ref(struct apu_checked_client *client);
void apu_checked_client_unref(struct apu_checked_client *client);
int apu_checked_client_close(struct apu_checked_client *client);
int apu_checked_client_query(struct apu_checked_client *client,
	struct apu_checked_client_snapshot *out);
int apu_checked_owner_query_id(struct apu_checked_owner *owner,
	unsigned int id, struct apu_checked_client_snapshot *out);
int apu_checked_get(struct apu_checked_client *client,
	struct apu_checked_lease **out);
int apu_checked_put(struct apu_checked_lease *lease);
struct apu_checked_lease *apu_checked_lease_ref(struct apu_checked_lease *lease);
void apu_checked_lease_unref(struct apu_checked_lease *lease);
/*
 * Provider policy only: fn is admitted only in ON. It must latch backend.fault
 * for every uncertain electrical/firmware failure; only pure input/policy
 * errno may leave ON. This is not a generic engine execution interface.
 */
int apu_checked_run(struct apu_checked_owner *owner,
	int (*fn)(void *arg), void *arg);

#endif
