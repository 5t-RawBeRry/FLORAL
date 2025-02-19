/*
 * Copyright (c) 2017-2018, 2020 The Linux Foundation. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/**
 *  DOC: HDD object manager API source file to create/destroy PSOC,
 *  PDEV, VDEV and PEER objects.
 */

#include <wlan_hdd_object_manager.h>
#include <wlan_osif_priv.h>
#include <wlan_reg_ucfg_api.h>
#include <target_if.h>
#include <os_if_spectral_netlink.h>

#define LOW_2GHZ_FREQ 2312
#define HIGH_2GHZ_FREQ 2732
#define LOW_5GHZ_FREQ  4912
#define HIGH_5GHZ_FREQ 6100

static void hdd_init_pdev_os_priv(struct hdd_context *hdd_ctx,
	struct pdev_osif_priv *os_priv)
{
	/* Initialize the OS private structure*/
	os_priv->wiphy = hdd_ctx->wiphy;
	os_priv->legacy_osif_priv = hdd_ctx;
	wlan_cfg80211_scan_priv_init(hdd_ctx->pdev);
	os_if_spectral_netlink_init(hdd_ctx->pdev);
}

static void hdd_deinit_pdev_os_priv(struct wlan_objmgr_pdev *pdev)
{
	os_if_spectral_netlink_deinit(pdev);
	wlan_cfg80211_scan_priv_deinit(pdev);
}

static struct vdev_osif_priv *
hdd_init_vdev_os_priv(struct hdd_adapter *adapter)
{
	struct vdev_osif_priv *os_priv;

	os_priv = qdf_mem_malloc(sizeof(*os_priv));
	if (!os_priv)
		return NULL;

	/* Initialize the vdev OS private structure*/
	os_priv->wdev = adapter->dev->ieee80211_ptr;
	os_priv->legacy_osif_priv = adapter;
	wlan_cfg80211_tdls_priv_init(os_priv);

	return os_priv;
}

static void hdd_deinit_vdev_os_priv(struct vdev_osif_priv *os_priv)
{
	if (os_priv) {
		wlan_cfg80211_tdls_priv_deinit(os_priv);
		qdf_mem_free(os_priv);
	}
}

static void hdd_init_psoc_qdf_ctx(struct wlan_objmgr_psoc *psoc)
{
	qdf_device_t qdf_ctx;

	qdf_ctx = cds_get_context(QDF_MODULE_ID_QDF_DEVICE);
	if (!qdf_ctx) {
		hdd_err("qdf ctx is null, can't set to soc object");
		return;
	}

	wlan_psoc_set_qdf_dev(psoc, qdf_ctx);
}

int hdd_objmgr_create_and_store_psoc(struct hdd_context *hdd_ctx,
				     uint8_t psoc_id)
{
	QDF_STATUS status;
	struct wlan_objmgr_psoc *psoc;

	psoc = wlan_objmgr_psoc_obj_create(psoc_id, WLAN_DEV_OL);
	if (!psoc)
		return -ENOMEM;

	status = wlan_objmgr_psoc_try_get_ref(psoc, WLAN_HDD_ID_OBJ_MGR);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_err("Failed to acquire psoc ref; status:%d", status);
		QDF_BUG(false);
		goto psoc_destroy;
	}

	hdd_init_psoc_qdf_ctx(psoc);
	hdd_ctx->psoc = psoc;

	return 0;

psoc_destroy:
	wlan_objmgr_psoc_obj_delete(psoc);

	return qdf_status_to_os_return(status);
}

int hdd_objmgr_release_and_destroy_psoc(struct hdd_context *hdd_ctx)
{
	QDF_STATUS status;
	struct wlan_objmgr_psoc *psoc = hdd_ctx->psoc;

	hdd_ctx->psoc = NULL;

	QDF_BUG(psoc);
	if (!psoc)
		return -EINVAL;

	wlan_objmgr_print_ref_all_objects_per_psoc(psoc);

	status = wlan_objmgr_psoc_obj_delete(psoc);
	wlan_objmgr_psoc_release_ref(psoc, WLAN_HDD_ID_OBJ_MGR);

	return qdf_status_to_os_return(status);
}

void hdd_objmgr_update_tgt_max_vdev_psoc(struct hdd_context *hdd_ctx,
					 uint8_t max_vdev)
{
	struct wlan_objmgr_psoc *psoc = hdd_ctx->psoc;

	if (!psoc) {
		hdd_err("Psoc NULL");
		return;
	}

	wlan_psoc_set_max_vdev_count(psoc, max_vdev);
}

int hdd_objmgr_create_and_store_pdev(struct hdd_context *hdd_ctx)
{
	QDF_STATUS status;
	struct wlan_objmgr_psoc *psoc = hdd_ctx->psoc;
	struct wlan_objmgr_pdev *pdev;
	struct pdev_osif_priv *priv;
	struct wlan_psoc_host_hal_reg_capabilities_ext *reg_cap_ptr;

	if (!psoc) {
		hdd_err("Psoc NULL");
		return -EINVAL;
	}

	priv = qdf_mem_malloc(sizeof(*priv));
	if (priv == NULL) {
		hdd_err("pdev os obj create failed");
		return -ENOMEM;
	}

	reg_cap_ptr = ucfg_reg_get_hal_reg_cap(psoc);
	if (!reg_cap_ptr) {
		hdd_err("Failed to get reg capability");
		status = QDF_STATUS_E_INVAL;
		goto free_priv;
	}
	reg_cap_ptr->phy_id = 0;
	reg_cap_ptr->low_2ghz_chan = LOW_2GHZ_FREQ;
	reg_cap_ptr->high_2ghz_chan = HIGH_2GHZ_FREQ;
	reg_cap_ptr->low_5ghz_chan = LOW_5GHZ_FREQ;
	reg_cap_ptr->high_5ghz_chan = HIGH_5GHZ_FREQ;

	pdev = wlan_objmgr_pdev_obj_create(psoc, priv);
	if (!pdev) {
		hdd_err("pdev obj create failed");
		status = QDF_STATUS_E_NOMEM;
		goto free_priv;
	}

	status = wlan_objmgr_pdev_try_get_ref(pdev, WLAN_HDD_ID_OBJ_MGR);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_err("Failed to acquire pdev ref; status:%d", status);
		QDF_BUG(false);
		goto pdev_destroy;
	}

	status = target_if_alloc_pdev_tgt_info(pdev);
	if (status != QDF_STATUS_SUCCESS) {
		hdd_err("pdev tgt info alloc failed");
		goto pdev_destroy;
	}

	hdd_ctx->pdev = pdev;
	sme_store_pdev(hdd_ctx->mac_handle, hdd_ctx->pdev);
	hdd_init_pdev_os_priv(hdd_ctx, priv);
	return 0;

pdev_destroy:
	wlan_objmgr_pdev_obj_delete(pdev);
free_priv:
	qdf_mem_free(priv);

	return qdf_status_to_os_return(status);
}

int hdd_objmgr_release_and_destroy_pdev(struct hdd_context *hdd_ctx)
{
	QDF_STATUS status;
	struct wlan_objmgr_pdev *pdev = hdd_ctx->pdev;
	struct pdev_osif_priv *osif_priv;

	hdd_ctx->pdev = NULL;

	QDF_BUG(pdev);
	if (!pdev)
		return -EINVAL;

	target_if_free_pdev_tgt_info(pdev);

	hdd_deinit_pdev_os_priv(pdev);
	osif_priv = wlan_pdev_get_ospriv(pdev);
	wlan_pdev_reset_ospriv(pdev);
	qdf_mem_free(osif_priv);

	status = wlan_objmgr_pdev_obj_delete(pdev);
	wlan_objmgr_pdev_release_ref(pdev, WLAN_HDD_ID_OBJ_MGR);

	return qdf_status_to_os_return(status);
}

int hdd_objmgr_create_and_store_vdev(struct wlan_objmgr_pdev *pdev,
				     struct hdd_adapter *adapter)
{
	QDF_STATUS status;
	int errno;
	struct wlan_objmgr_vdev *vdev;
	struct vdev_osif_priv *osif_priv;
	struct wlan_vdev_create_params vdev_params = {0};

	QDF_BUG(pdev);
	if (!pdev) {
		hdd_err("pdev is null");
		return -EINVAL;
	}

	osif_priv = hdd_init_vdev_os_priv(adapter);
	if (!osif_priv) {
		hdd_err("Failed to allocate osif_priv; out of memory");
		return -ENOMEM;
	}

	vdev_params.opmode = adapter->device_mode;
	vdev_params.osifp = osif_priv;
	qdf_mem_copy(vdev_params.macaddr,
		     adapter->mac_addr.bytes,
		     QDF_NET_MAC_ADDR_MAX_LEN);

	vdev = wlan_objmgr_vdev_obj_create(pdev, &vdev_params);
	if (!vdev) {
		hdd_err("Failed to create vdev object");
		errno = -ENOMEM;
		goto osif_priv_free;
	}

	/*
	 * To enable legacy use cases, we need to delay physical vdev destroy
	 * until after the sme session has been closed. We accomplish this by
	 * getting a reference here.
	 */
	status = wlan_objmgr_vdev_try_get_ref(vdev, WLAN_HDD_ID_OBJ_MGR);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_err("Failed to acquire vdev ref; status:%d", status);
		errno = qdf_status_to_os_return(status);
		goto vdev_destroy;
	}

	qdf_spin_lock_bh(&adapter->vdev_lock);
	adapter->vdev = vdev;
	adapter->session_id = wlan_vdev_get_id(vdev);
	qdf_spin_unlock_bh(&adapter->vdev_lock);

	return 0;

vdev_destroy:
	wlan_objmgr_vdev_obj_delete(vdev);

osif_priv_free:
	hdd_deinit_vdev_os_priv(osif_priv);

	return errno;
}

int hdd_objmgr_release_and_destroy_vdev(struct hdd_adapter *adapter)
{
	QDF_STATUS status;
	struct wlan_objmgr_vdev *vdev;
	struct vdev_osif_priv *osif_priv;

	qdf_spin_lock_bh(&adapter->vdev_lock);
	vdev = adapter->vdev;
	adapter->vdev = NULL;
	adapter->session_id = HDD_SESSION_ID_INVALID;
	qdf_spin_unlock_bh(&adapter->vdev_lock);

	QDF_BUG(vdev);
	if (!vdev)
		return -EINVAL;

	osif_priv = wlan_vdev_get_ospriv(vdev);
	wlan_vdev_reset_ospriv(vdev);

	QDF_BUG(osif_priv);
	hdd_deinit_vdev_os_priv(osif_priv);

	status = wlan_objmgr_vdev_obj_delete(vdev);
	wlan_objmgr_vdev_release_ref(vdev, WLAN_HDD_ID_OBJ_MGR);

	return qdf_status_to_os_return(status);
}

struct wlan_objmgr_vdev *__hdd_objmgr_get_vdev(struct hdd_adapter *adapter,
					       const char *func)
{
	struct wlan_objmgr_vdev *vdev = NULL;
	QDF_STATUS status;

	if (!adapter) {
		hdd_err("Adapter is NULL (via %s)", func);
		return NULL;
	}

	qdf_spin_lock_bh(&adapter->vdev_lock);
	vdev = adapter->vdev;
	if (vdev) {
		status = wlan_objmgr_vdev_try_get_ref(vdev, WLAN_OSIF_ID);
		if (QDF_IS_STATUS_ERROR(status))
			vdev = NULL;
	}
	qdf_spin_unlock_bh(&adapter->vdev_lock);

	if (!vdev)
		hdd_debug("VDEV is NULL (via %s)", func);

	return vdev;
}

void __hdd_objmgr_put_vdev(struct wlan_objmgr_vdev *vdev, const char *func)
{
	if (!vdev) {
		hdd_err("VDEV is NULL (via %s)", func);
		return;
	}

	wlan_objmgr_vdev_release_ref(vdev, WLAN_OSIF_ID);
}

int hdd_objmgr_set_peer_mlme_auth_state(struct wlan_objmgr_vdev *vdev,
					bool is_authenticated)
{
	struct wlan_objmgr_peer *peer;
	QDF_STATUS status;

	wlan_vdev_obj_lock(vdev);
	peer = wlan_vdev_get_bsspeer(vdev);
	wlan_vdev_obj_unlock(vdev);

	if (!peer) {
		hdd_err("peer is null");

		return -EINVAL;
	}
	status = wlan_objmgr_peer_try_get_ref(peer, WLAN_TDLS_NB_ID);
	if (status != QDF_STATUS_SUCCESS)
		return -EINVAL;

	wlan_peer_obj_lock(peer);
	wlan_peer_mlme_set_auth_state(peer, is_authenticated);
	wlan_peer_obj_unlock(peer);

	wlan_objmgr_peer_release_ref(peer, WLAN_TDLS_NB_ID);
	return 0;
}

int hdd_objmgr_set_peer_mlme_state(struct wlan_objmgr_vdev *vdev,
	enum wlan_peer_state peer_state)
{
	struct wlan_objmgr_peer *peer;

	wlan_vdev_obj_lock(vdev);
	peer = wlan_vdev_get_bsspeer(vdev);
	wlan_vdev_obj_unlock(vdev);

	if (!peer) {
		hdd_err("peer is null");

		return -EINVAL;
	}

	wlan_peer_obj_lock(peer);
	wlan_peer_mlme_set_state(peer, WLAN_ASSOC_STATE);
	wlan_peer_obj_unlock(peer);

	return 0;
}

