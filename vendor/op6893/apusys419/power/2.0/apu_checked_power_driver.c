// SPDX-License-Identifier: GPL-2.0
/* Software owner adapter. The enclosing hardware probe remains blocked. */
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/mutex.h>

#include "apu_checked_power_driver.h"
#include "apusys_power.h"
#include "apusys_power_ctl.h"
#include "hal_config_power.h"

static DEFINE_MUTEX(checked_publication_lock);
static DEFINE_MUTEX(checked_detach_lock);
static struct apu_checked_owner *checked_owner;
/* Once selected, a late entry must never fall back to legacy hardware APIs
 * when detach withdraws the published pointer. Reset only by module lifetime.
 */
static bool checked_mode;
/* A failed detach keeps the create reference and backend context reachable. */
static struct apu_checked_owner *retiring_owner;

static bool checked_user_valid(enum DVFS_USER user)
{
	return (user >= VPU0 && user < APUSYS_DVFS_USER_NUM) ||
		user == EDMA || user == EDMA2 || user == REVISER;
}

static int checked_backend_on(void *ctx, unsigned int id, bool first)
{
	(void)ctx;
	(void)first;
	return apusys_power_on(id);
}

static int checked_backend_off(void *ctx, unsigned int id, bool last)
{
	(void)ctx;
	(void)last;
	return apusys_power_off(id);
}

static int checked_backend_fault(void *ctx)
{
	(void)ctx;
	return apusys_power_transition_error();
}

/* The publication lock never spans a core operation or callback. A backend
 * fault record can query cached status while another API waits for its turn.
 */
static struct apu_checked_owner *checked_owner_get(void)
{
	struct apu_checked_owner *owner = NULL;

	mutex_lock(&checked_publication_lock);
	if (apusys_power_check() && checked_owner)
		owner = apu_checked_owner_ref(checked_owner);
	mutex_unlock(&checked_publication_lock);
	return owner;
}

bool apusys_checked_power_present(void)
{
	return READ_ONCE(checked_mode);
}

int apu_checked_driver_init(struct device *device)
{
	struct apu_checked_backend backend = {
		.on = checked_backend_on,
		.off = checked_backend_off,
		.fault = checked_backend_fault,
	};
	struct apu_checked_owner *new_owner;
	int ret;

	mutex_lock(&checked_publication_lock);
	if (checked_owner || retiring_owner)
		ret = -EBUSY;
	else {
		ret = apu_checked_owner_create(device, THIS_MODULE, &backend,
					      &new_owner);
		if (!ret) {
			WRITE_ONCE(checked_owner, new_owner);
			WRITE_ONCE(checked_mode, true);
		}
	}
	mutex_unlock(&checked_publication_lock);
	return ret;
}

int apu_power_checked_client_register(enum DVFS_USER user, struct device *device,
	struct module *module, const struct apu_checked_client_ops *ops, void *ctx,
	struct apu_checked_client **out)
{
	struct apu_checked_owner *owner;
	int ret;

	owner = checked_owner_get();
	if (!owner)
		return -EPROBE_DEFER;
	if (!checked_user_valid(user))
		ret = -EINVAL;
	else
		ret = apu_checked_client_register(owner, user, device, module,
						 ops, ctx, out);
	apu_checked_owner_unref(owner);
	return ret;
}
EXPORT_SYMBOL_GPL(apu_power_checked_client_register);

int apu_power_checked_owner_query(struct apu_checked_snapshot *out)
{
	struct apu_checked_owner *owner;
	int ret;

	owner = checked_owner_get();
	if (!owner)
		return -ENODEV;
	ret = apu_checked_owner_query(owner, out);
	apu_checked_owner_unref(owner);
	return ret;
}
EXPORT_SYMBOL_GPL(apu_power_checked_owner_query);

int apu_power_checked_error(void)
{
	struct apu_checked_snapshot snapshot;
	int ret = apusys_power_transition_error();

	if (ret || !apusys_checked_power_present())
		return ret;
	ret = apu_power_checked_owner_query(&snapshot);
	return ret ?: snapshot.error;
}

bool apusys_checked_power_status(enum DVFS_USER user)
{
	struct apu_checked_client_snapshot snapshot;
	struct apu_checked_owner *owner;
	bool on = false;

	if (!checked_user_valid(user) || apusys_power_transition_error())
		return false;
	owner = checked_owner_get();
	if (!owner)
		return false;
	if (!apu_checked_owner_query_id(owner, user, &snapshot))
		on = snapshot.owner_state == APU_CHECKED_ON &&
			snapshot.registered && snapshot.leases && !snapshot.uncertain;
	apu_checked_owner_unref(owner);
	return on;
}

int apu_checked_driver_run(int (*fn)(void *arg), void *arg)
{
	struct apu_checked_owner *owner;
	int ret;

	owner = checked_owner_get();
	if (!owner)
		return -ENODEV;
	ret = apu_checked_run(owner, fn, arg);
	apu_checked_owner_unref(owner);
	return ret;
}

int apu_checked_driver_detach(void)
{
	struct apu_checked_owner *owner;
	int ret;

	mutex_lock(&checked_detach_lock);
	mutex_lock(&checked_publication_lock);
	owner = checked_owner ?: retiring_owner;
	WRITE_ONCE(checked_owner, NULL);
	retiring_owner = owner;
	mutex_unlock(&checked_publication_lock);
	if (!owner) {
		ret = 0;
		goto unlock;
	}
	/* Close admission before draining. Existing client/lease references keep
	 * the owner alive; a busy/failed owner retains its create reference here.
	 */
	ret = apu_checked_owner_stop(owner);
	if (ret)
		goto unlock;
	ret = apu_checked_owner_destroy(owner);
	if (!ret) {
		mutex_lock(&checked_publication_lock);
		retiring_owner = NULL;
		mutex_unlock(&checked_publication_lock);
	}
unlock:
	mutex_unlock(&checked_detach_lock);
	return ret;
}

/* Real checked operations, separate from the incompatible legacy void APIs. */
EXPORT_SYMBOL_GPL(apu_checked_get);
EXPORT_SYMBOL_GPL(apu_checked_put);
EXPORT_SYMBOL_GPL(apu_checked_client_ref);
EXPORT_SYMBOL_GPL(apu_checked_client_unref);
EXPORT_SYMBOL_GPL(apu_checked_client_close);
EXPORT_SYMBOL_GPL(apu_checked_client_query);
EXPORT_SYMBOL_GPL(apu_checked_lease_ref);
EXPORT_SYMBOL_GPL(apu_checked_lease_unref);
