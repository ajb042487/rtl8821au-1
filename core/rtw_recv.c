/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#define _RTW_RECV_C_

#include <linux/etherdevice.h>
#include <drv_types.h>
#include <rtw_ap.h>

#undef DBG_871X
static inline void DBG_871X(const char *fmt, ...)
{
}

static struct list_head *get_next(struct list_head	*list)
{
	return list->next;
}

#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
void rtw_signal_stat_timer_hdl(RTW_TIMER_HDL_ARGS);
#endif


void _rtw_init_sta_recv_priv(struct sta_recv_priv *psta_recvpriv)
{
	memset((uint8_t *)psta_recvpriv, 0, sizeof (struct sta_recv_priv));

	spin_lock_init(&psta_recvpriv->lock);

	/*
	 * for(i=0; i<MAX_RX_NUMBLKS; i++)
	 * 	_rtw_init_queue(&psta_recvpriv->blk_strms[i]);
	 */

	_rtw_init_queue(&psta_recvpriv->defrag_q);

}

int _rtw_init_recv_priv(struct recv_priv *precvpriv, struct rtl_priv *rtlpriv)
{
	int i;

	struct recv_frame *precvframe;

	int	res=_SUCCESS;


	/*
	 * We don't need to memset rtlpriv->XXX to zero, because rtlpriv is allocated by rtw_zvmalloc().
	 * memset((unsigned char *)precvpriv, 0, sizeof (struct  recv_priv));
	 */

	spin_lock_init(&precvpriv->lock);

	_rtw_init_queue(&precvpriv->free_recv_queue);
	_rtw_init_queue(&precvpriv->recv_pending_queue);
	_rtw_init_queue(&precvpriv->uc_swdec_pending_queue);

	precvpriv->rtlpriv = rtlpriv;

	precvpriv->free_recvframe_cnt = NR_RECVFRAME;

	precvpriv->pallocated_frame_buf = rtw_zvmalloc(NR_RECVFRAME * sizeof(struct recv_frame) + RXFRAME_ALIGN_SZ);

	if (precvpriv->pallocated_frame_buf==NULL){
		res= _FAIL;
		goto exit;
	}

	/*
	 * memset(precvpriv->pallocated_frame_buf, 0, NR_RECVFRAME * sizeof(struct recv_frame) + RXFRAME_ALIGN_SZ);
	 */

	precvpriv->precv_frame_buf = (uint8_t *)N_BYTE_ALIGMENT((SIZE_PTR)(precvpriv->pallocated_frame_buf), RXFRAME_ALIGN_SZ);

	/*
	 * precvpriv->precv_frame_buf = precvpriv->pallocated_frame_buf + RXFRAME_ALIGN_SZ -
	 * 						((SIZE_PTR) (precvpriv->pallocated_frame_buf) &(RXFRAME_ALIGN_SZ-1));
	 */

	precvframe = (struct recv_frame*) precvpriv->precv_frame_buf;

	for (i = 0; i < NR_RECVFRAME ; i++) {
		INIT_LIST_HEAD(&(precvframe->list));

		list_add_tail(&(precvframe->list), &(precvpriv->free_recv_queue.list));

		precvframe->skb = NULL;
		precvframe->len = 0;

		precvframe->rtlpriv =rtlpriv;
		precvframe++;

	}


	precvpriv->rx_pending_cnt=1;

	sema_init(&precvpriv->allrxreturnevt, 0);


	res = rtlpriv->cfg->ops->init_recv_priv(rtlpriv);

#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
	_init_timer(&precvpriv->signal_stat_timer,
		    RTW_TIMER_HDL_NAME(signal_stat), rtlpriv);

	precvpriv->signal_stat_sampling_interval = 1000; /* ms */
	/* precvpriv->signal_stat_converging_constant = 5000; ms */

	rtw_set_signal_stat_timer(precvpriv);
#endif

exit:



	return res;

}

void _rtw_free_recv_priv (struct recv_priv *precvpriv)
{
	struct rtl_priv	*rtlpriv = precvpriv->rtlpriv;

	rtw_free_uc_swdec_pending_queue(rtlpriv);

	if(precvpriv->pallocated_frame_buf) {
		rtw_vmfree(precvpriv->pallocated_frame_buf);
	}

	rtlpriv->cfg->ops->free_recv_priv(rtlpriv);
}

struct recv_frame *_rtw_alloc_recvframe (struct __queue *pfree_recv_queue)
{

	struct recv_frame  *precvframe;
	struct list_head	*plist, *phead;
	struct rtl_priv *rtlpriv;
	struct recv_priv *precvpriv;

	if (list_empty(&pfree_recv_queue->list)) {
		precvframe = NULL;
	} else {
		phead = get_list_head(pfree_recv_queue);

		plist = get_next(phead);

		precvframe = container_of(plist, struct recv_frame, list);

		list_del_init(&precvframe->list);
		rtlpriv=precvframe->rtlpriv;
		if(rtlpriv !=NULL){
			precvpriv=&rtlpriv->recvpriv;
			if(pfree_recv_queue == &precvpriv->free_recv_queue)
				precvpriv->free_recvframe_cnt--;
		}
	}

	return precvframe;
}

struct recv_frame *rtw_alloc_recvframe (struct __queue *pfree_recv_queue)
{
	struct recv_frame  *precvframe;

	spin_lock_bh(&pfree_recv_queue->lock);

	precvframe = _rtw_alloc_recvframe(pfree_recv_queue);

	spin_unlock_bh(&pfree_recv_queue->lock);

	return precvframe;
}

void rtw_init_recvframe(struct recv_frame *precvframe, struct recv_priv *precvpriv)
{
	/* Perry: This can be removed */
	INIT_LIST_HEAD(&precvframe->list);

	precvframe->len=0;
}

int rtw_free_recvframe(struct recv_frame *precvframe, struct __queue *pfree_recv_queue)
{
	struct rtl_priv *rtlpriv=precvframe->rtlpriv;
	struct recv_priv *precvpriv = &rtlpriv->recvpriv;



	rtw_os_free_recvframe(precvframe);

	spin_lock_bh(&pfree_recv_queue->lock);

	list_del_init(&(precvframe->list));

	precvframe->len = 0;

	list_add_tail(&(precvframe->list), get_list_head(pfree_recv_queue));

	if (rtlpriv !=NULL){
		if(pfree_recv_queue == &precvpriv->free_recv_queue)
				precvpriv->free_recvframe_cnt++;
	}

	spin_unlock_bh(&pfree_recv_queue->lock);

	return _SUCCESS;
}




int _rtw_enqueue_recvframe(struct recv_frame *precvframe, struct __queue *queue)
{

	struct rtl_priv *rtlpriv=precvframe->rtlpriv;
	struct recv_priv *precvpriv = &rtlpriv->recvpriv;

	/*INIT_LIST_HEAD(&(precvframe->u.hdr.list)); */
	list_del_init(&(precvframe->list));
	list_add_tail(&(precvframe->list), get_list_head(queue));

	if (rtlpriv != NULL) {
		if (queue == &precvpriv->free_recv_queue)
			precvpriv->free_recvframe_cnt++;
	}

	return _SUCCESS;
}

int rtw_enqueue_recvframe(struct recv_frame *precvframe, struct __queue *queue)
{
	int ret;

	/* _spinlock(&pfree_recv_queue->lock); */
	spin_lock_bh(&queue->lock);
	ret = _rtw_enqueue_recvframe(precvframe, queue);
	/* spin_unlock(&pfree_recv_queue->lock); */
	spin_unlock_bh(&queue->lock);

	return ret;
}

/*
int	rtw_enqueue_recvframe(struct recv_frame *precvframe, struct __queue *queue)
{
	return rtw_free_recvframe(precvframe, queue);
}
*/




/*
caller : defrag ; recvframe_chk_defrag in recv_thread  (passive)
pframequeue: defrag_queue : will be accessed in recv_thread  (passive)

using spinlock to protect

*/

void rtw_free_recvframe_queue(struct __queue *pframequeue,  struct __queue *pfree_recv_queue)
{
	struct recv_frame *precvframe;
	struct list_head	*plist, *phead;

	spin_lock(&pframequeue->lock);

	phead = get_list_head(pframequeue);
	plist = get_next(phead);

	while (rtw_end_of_queue_search(phead, plist) == false) {
		precvframe = container_of(plist, struct recv_frame, list);

		plist = get_next(plist);

		/*
		 * list_del_init(&precvframe->u.hdr.list);
		 * will do this in rtw_free_recvframe()
		 */

		rtw_free_recvframe(precvframe, pfree_recv_queue);
	}

	spin_unlock(&pframequeue->lock);


}

uint32_t rtw_free_uc_swdec_pending_queue(struct rtl_priv *rtlpriv)
{
	uint32_t cnt = 0;
	struct recv_frame *pending_frame;

	while ((pending_frame=rtw_alloc_recvframe(&rtlpriv->recvpriv.uc_swdec_pending_queue))) {
		rtw_free_recvframe(pending_frame, &rtlpriv->recvpriv.free_recv_queue);
		DBG_871X("%s: dequeue uc_swdec_pending_queue\n", __func__);
		cnt++;
	}

	return cnt;
}


int rtw_enqueue_recvbuf_to_head(struct recv_buf *precvbuf, struct __queue *queue)
{
	spin_lock_bh(&queue->lock);

	list_del_init(&precvbuf->list);
	list_add(&precvbuf->list, get_list_head(queue));

	spin_unlock_bh(&queue->lock);

	return _SUCCESS;
}

int rtw_enqueue_recvbuf(struct recv_buf *precvbuf, struct __queue *queue)
{
	unsigned long flags;

	spin_lock_irqsave(&queue->lock, flags);

	list_del_init(&precvbuf->list);
	list_add_tail(&precvbuf->list, get_list_head(queue));
	spin_unlock_irqrestore(&queue->lock, flags);
	return _SUCCESS;
}

struct recv_buf *rtw_dequeue_recvbuf (struct __queue *queue)
{
	unsigned long flags;
	struct recv_buf *precvbuf;
	struct list_head	*plist, *phead;

	spin_lock_irqsave(&queue->lock, flags);

	if(list_empty(&queue->list)) {
		precvbuf = NULL;
	} else {
		phead = get_list_head(queue);

		plist = get_next(phead);

		precvbuf = container_of(plist, struct recv_buf, list);

		list_del_init(&precvbuf->list);

	}

	spin_unlock_irqrestore(&queue->lock, flags);

	return precvbuf;

}

int recvframe_chkmic(struct rtl_priv *rtlpriv,  struct recv_frame *precvframe);
int recvframe_chkmic(struct rtl_priv *rtlpriv,  struct recv_frame *precvframe){

	int	i,res=_SUCCESS;
	uint32_t	datalen;
	uint8_t	miccode[8];
	uint8_t	bmic_err=false,brpt_micerror = true;
	uint8_t	*pframe, *payload,*pframemic;
	uint8_t	*mickey;
	/* uint8_t	*iv,rxdata_key_idx=0; */
	struct	sta_info		*stainfo;
	struct	rx_pkt_attrib	*prxattrib=&precvframe->attrib;
	struct 	security_priv	*psecuritypriv=&rtlpriv->securitypriv;

	struct mlme_ext_priv	*pmlmeext = &rtlpriv->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	stainfo=rtw_get_stainfo(&rtlpriv->stapriv ,&prxattrib->ta[0]);

	if (prxattrib->encrypt == TKIP_ENCRYPTION) {
		/* calculate mic code */
		if (stainfo != NULL) {
			if (is_multicast_ether_addr(prxattrib->ra)) {
				/*
				 * mickey=&psecuritypriv->dot118021XGrprxmickey.skey[0];
				 * iv = precvframe->u.hdr.rx_data+prxattrib->hdrlen;
				 * rxdata_key_idx =( ((iv[3])>>6)&0x3) ;
				 * mickey=&psecuritypriv->dot118021XGrprxmickey[prxattrib->key_index].skey[0];
				 */

				/*
				 * DBG_871X("\n recvframe_chkmic: bcmc key psecuritypriv->dot118021XGrpKeyid(%d),pmlmeinfo->key_index(%d) ,recv key_id(%d)\n",
				 * psecuritypriv->dot118021XGrpKeyid,pmlmeinfo->key_index,rxdata_key_idx);
				 */

				if (psecuritypriv->binstallGrpkey == false) {
					res=_FAIL;
					DBG_871X("\n recvframe_chkmic:didn't install group key!!!!!!!!!!\n");
					goto exit;
				}
			} else{
				mickey=&stainfo->dot11tkiprxmickey.skey[0];
			}

			datalen=precvframe->len-prxattrib->hdrlen-prxattrib->iv_len-prxattrib->icv_len-8;//icv_len included the mic code
			pframe=precvframe->rx_data;
			payload=pframe+prxattrib->hdrlen+prxattrib->iv_len;

			/*
			 * rtw_seccalctkipmic(&stainfo->dot11tkiprxmickey.skey[0],pframe,payload, datalen ,&miccode[0],(unsigned char)prxattrib->priority); //care the length of the data
			 */

			rtw_seccalctkipmic(mickey,pframe,payload, datalen ,&miccode[0],(unsigned char)prxattrib->priority); //care the length of the data

			pframemic=payload+datalen;

			bmic_err=false;

			for (i = 0; i < 8; i++) {
				if (miccode[i] != *(pframemic+i)){
					bmic_err=true;
				}
			}


			if (bmic_err == true) {
				{
					uint i;
					for(i=0;i<precvframe->len;i=i+8){
						;
					}
				}


				/*
				 * double check key_index for some timing issue ,
				 * cannot compare with psecuritypriv->dot118021XGrpKeyid also cause timing issue
				 */

				if((is_multicast_ether_addr(prxattrib->ra)==true)  && (prxattrib->key_index != pmlmeinfo->key_index ))
					brpt_micerror = false;

				if((prxattrib->bdecrypted ==true)&& (brpt_micerror == true)) {
					rtw_handle_tkip_mic_err(rtlpriv,(uint8_t)is_multicast_ether_addr(prxattrib->ra));
					DBG_871X(" mic error :prxattrib->bdecrypted=%d\n",prxattrib->bdecrypted);
				} else {
					DBG_871X(" mic error :prxattrib->bdecrypted=%d\n",prxattrib->bdecrypted);
				}

				res=_FAIL;

			} else {
				/* mic checked ok */
				if((psecuritypriv->bcheck_grpkey ==false)&&(is_multicast_ether_addr(prxattrib->ra)==true)){
					psecuritypriv->bcheck_grpkey =true;
				}
			}

		} else 	{
		}

		recvframe_pull_tail(precvframe, 8);

	}

exit:

	return res;

}

//decrypt and set the ivlen,icvlen of the recv_frame
struct recv_frame * decryptor(struct rtl_priv *rtlpriv,struct recv_frame *precv_frame);
struct recv_frame * decryptor(struct rtl_priv *rtlpriv,struct recv_frame *precv_frame)
{

	struct rx_pkt_attrib *prxattrib = &precv_frame->attrib;
	struct security_priv *psecuritypriv=&rtlpriv->securitypriv;
	struct recv_frame *return_packet=precv_frame;
	uint32_t	 res=_SUCCESS;

	if (prxattrib->encrypt>0) {
		uint8_t *iv = precv_frame->rx_data+prxattrib->hdrlen;
		prxattrib->key_index = ( ((iv[3])>>6)&0x3) ;

		if(prxattrib->key_index > WEP_KEYS) {
			DBG_871X("prxattrib->key_index(%d) > WEP_KEYS \n", prxattrib->key_index);

			switch(prxattrib->encrypt){
			case WEP40_ENCRYPTION:
			case WEP104_ENCRYPTION:
				prxattrib->key_index = psecuritypriv->dot11PrivacyKeyIndex;
				break;
			case TKIP_ENCRYPTION:
			case AESCCMP_ENCRYPTION:
			default:
				prxattrib->key_index = psecuritypriv->dot118021XGrpKeyid;
				break;
			}
		}
	}

	if ((prxattrib->encrypt>0) && ((prxattrib->bdecrypted==0) ||(psecuritypriv->sw_decrypt==true))) {

		psecuritypriv->hw_decrypted=false;

#ifdef DBG_RX_DECRYPTOR
		DBG_871X("prxstat->bdecrypted:%d,  prxattrib->encrypt:%d,  Setting psecuritypriv->hw_decrypted = %d\n"
			, prxattrib->bdecrypted ,prxattrib->encrypt, psecuritypriv->hw_decrypted);
#endif

		switch (prxattrib->encrypt) {
		case WEP40_ENCRYPTION:
		case WEP104_ENCRYPTION:
			rtw_wep_decrypt(rtlpriv, precv_frame);
			break;
		case TKIP_ENCRYPTION:
			res = rtw_tkip_decrypt(rtlpriv, precv_frame);
			break;
		case AESCCMP_ENCRYPTION:
			res = rtw_aes_decrypt(rtlpriv, precv_frame);
			break;
		default:
				break;
		}
	} else if(prxattrib->bdecrypted==1
		&& prxattrib->encrypt >0
		&& (psecuritypriv->busetkipkey==1 || prxattrib->encrypt !=TKIP_ENCRYPTION )
		)
	{
		{
			psecuritypriv->hw_decrypted=true;
			#ifdef DBG_RX_DECRYPTOR
			DBG_871X("prxstat->bdecrypted:%d,  prxattrib->encrypt:%d,  Setting psecuritypriv->hw_decrypted = %d\n"
			, prxattrib->bdecrypted ,prxattrib->encrypt, psecuritypriv->hw_decrypted);
			#endif

		}
	} else {
		#ifdef DBG_RX_DECRYPTOR
		DBG_871X("prxstat->bdecrypted:%d,  prxattrib->encrypt:%d,  psecuritypriv->hw_decrypted:%d\n"
		, prxattrib->bdecrypted ,prxattrib->encrypt, psecuritypriv->hw_decrypted);
		#endif
	}

	if(res == _FAIL) {
		rtw_free_recvframe(return_packet,&rtlpriv->recvpriv.free_recv_queue);
		return_packet = NULL;

	}
	//recvframe_chkmic(rtlpriv, precv_frame);   //move to recvframme_defrag function

	return return_packet;

}
//###set the security information in the recv_frame
struct recv_frame *portctrl(struct rtl_priv *rtlpriv,struct recv_frame * precv_frame);
struct recv_frame * portctrl(struct rtl_priv *rtlpriv,struct recv_frame * precv_frame)
{
	uint8_t   *psta_addr, *ptr;
	uint  auth_alg;
	struct sta_info *psta;
	struct sta_priv *pstapriv ;
	struct recv_frame *prtnframe;
	u16	ether_type=0;
	u16  eapol_type = 0x888e;//for Funia BD's WPA issue
	struct rx_pkt_attrib *pattrib;

	pstapriv = &rtlpriv->stapriv;
	psta = rtw_get_stainfo(pstapriv, psta_addr);

	auth_alg = rtlpriv->securitypriv.dot11AuthAlgrthm;

	ptr = get_recvframe_data(precv_frame);
	pattrib = &precv_frame->attrib;
	psta_addr = pattrib->ta;

	prtnframe = NULL;

	if (auth_alg == 2) {
		if ((psta != NULL) && (psta->ieee8021x_blocked)) {
			/*
			 * blocked
			 * only accept EAPOL frame
			 */
			prtnframe=precv_frame;

			/* get ether_type */
			ptr=ptr+precv_frame->attrib.hdrlen+precv_frame->attrib.iv_len+LLC_HEADER_SIZE;
			memcpy(&ether_type,ptr, 2);
			ether_type= ntohs((unsigned short )ether_type);

		        if (ether_type == eapol_type) {
				prtnframe=precv_frame;
			} else {
				//free this frame
				rtw_free_recvframe(precv_frame, &rtlpriv->recvpriv.free_recv_queue);
				prtnframe=NULL;
			}
		} else {
			/*
			 * allowed
			 * check decryption status, and decrypt the frame if needed
			 */

			if (pattrib->bdecrypted == 0) {
				;
			}

			prtnframe=precv_frame;
			/* check is the EAPOL frame or not (Rekey) */
			if (ether_type == eapol_type) {

				/* check Rekey */

				prtnframe=precv_frame;
			} else {
				;
			}
		}
	} else {
		prtnframe=precv_frame;
	}

	return prtnframe;

}

int recv_decache(struct recv_frame *precv_frame, uint8_t bretry, struct stainfo_rxcache *prxcache);
int recv_decache(struct recv_frame *precv_frame, uint8_t bretry, struct stainfo_rxcache *prxcache)
{
	int tid = precv_frame->attrib.priority;

	u16 seq_ctrl = ( (precv_frame->attrib.seq_num&0xffff) << 4) |
		(precv_frame->attrib.frag_num & 0xf);

	if (tid > 15) {
		return _FAIL;
	}

	if (1) { 	/* if(bretry) */
		if(seq_ctrl == prxcache->tid_rxseq[tid]) {
			return _FAIL;
		}
	}

	prxcache->tid_rxseq[tid] = seq_ctrl;

	return _SUCCESS;

}

void process_pwrbit_data(struct rtl_priv *rtlpriv, struct recv_frame *precv_frame);
void process_pwrbit_data(struct rtl_priv *rtlpriv, struct recv_frame *precv_frame)
{
#ifdef CONFIG_AP_MODE
	unsigned char pwrbit;
	uint8_t *ptr = precv_frame->rx_data;
	struct rx_pkt_attrib *pattrib = &precv_frame->attrib;
	struct sta_priv *pstapriv = &rtlpriv->stapriv;
	struct sta_info *psta=NULL;

	psta = rtw_get_stainfo(pstapriv, pattrib->src);

	pwrbit = GetPwrMgt(ptr);

	if (psta) {
		if (pwrbit) {
			if (!(psta->state & WIFI_SLEEP_STATE)) {
				/*
				 * psta->state |= WIFI_SLEEP_STATE;
				 * pstapriv->sta_dz_bitmap |= BIT(psta->aid);
				 */

				stop_sta_xmit(rtlpriv, psta);

				/* DBG_871X("to sleep, sta_dz_bitmap=%x\n", pstapriv->sta_dz_bitmap); */
			}
		} else {
			if (psta->state & WIFI_SLEEP_STATE) {
				/*
				 * psta->state ^= WIFI_SLEEP_STATE;
				 * pstapriv->sta_dz_bitmap &= ~BIT(psta->aid);
				 */

				 wakeup_sta_to_xmit(rtlpriv, psta);

				/*
				 *DBG_871X("to wakeup, sta_dz_bitmap=%x\n", pstapriv->sta_dz_bitmap);
				 */

			}
		}

	}

#endif
}

void process_wmmps_data(struct rtl_priv *rtlpriv, struct recv_frame *precv_frame);
void process_wmmps_data(struct rtl_priv *rtlpriv, struct recv_frame *precv_frame)
{
#ifdef CONFIG_AP_MODE
	struct rx_pkt_attrib *pattrib = &precv_frame->attrib;
	struct sta_priv *pstapriv = &rtlpriv->stapriv;
	struct sta_info *psta=NULL;

	psta = rtw_get_stainfo(pstapriv, pattrib->src);

	if (!psta)
		return;


	if(!psta->qos_option)
		return;

	if(!(psta->qos_info&0xf))
		return;


	if (psta->state & WIFI_SLEEP_STATE) {
		uint8_t wmmps_ac=0;

		switch (pattrib->priority) {
		case 1:
		case 2:
			wmmps_ac = psta->uapsd_bk&BIT(1);
			break;
		case 4:
		case 5:
			wmmps_ac = psta->uapsd_vi&BIT(1);
			break;
		case 6:
		case 7:
			wmmps_ac = psta->uapsd_vo&BIT(1);
			break;
		case 0:
		case 3:
		default:
			wmmps_ac = psta->uapsd_be&BIT(1);
			break;
		}

		if (wmmps_ac) {
			if (psta->sleepq_ac_len>0) {
				/* process received triggered frame */
				xmit_delivery_enabled_frames(rtlpriv, psta);
			} else {
				/* issue one qos null frame with More data bit = 0 and the EOSP bit set (=1) */
				issue_qos_nulldata(rtlpriv, psta->hwaddr, (u16)pattrib->priority, 0, 0);
			}
		}

	}


#endif

}


void count_rx_stats(struct rtl_priv *rtlpriv, struct recv_frame *prframe, struct sta_info*sta);
void count_rx_stats(struct rtl_priv *rtlpriv, struct recv_frame *prframe, struct sta_info*sta)
{
	int	sz;
	struct sta_info		*psta = NULL;
	struct stainfo_stats	*pstats = NULL;
	struct rx_pkt_attrib	*pattrib = & prframe->attrib;
	struct recv_priv		*precvpriv = &rtlpriv->recvpriv;

	sz = get_recvframe_len(prframe);
	precvpriv->rx_bytes += sz;

	rtlpriv->mlmepriv.LinkDetectInfo.NumRxOkInPeriod++;

	if( (!MacAddr_isBcst(pattrib->dst)) && (!is_multicast_ether_addr(pattrib->dst))){
		rtlpriv->mlmepriv.LinkDetectInfo.NumRxUnicastOkInPeriod++;
	}

	if(sta)
		psta = sta;
	else
		psta = prframe->psta;

	if(psta) {
		pstats = &psta->sta_stats;

		pstats->rx_data_pkts++;
		pstats->rx_bytes += sz;
	}

}

int sta2sta_data_frame(
	struct rtl_priv *rtlpriv,
	struct recv_frame *precv_frame,
	struct sta_info**psta
);

int sta2sta_data_frame(
	struct rtl_priv *rtlpriv,
	struct recv_frame *precv_frame,
	struct sta_info**psta)
{
	uint8_t *ptr = precv_frame->rx_data;
	int ret = _SUCCESS;
	struct rx_pkt_attrib *pattrib = & precv_frame->attrib;
	struct	sta_priv 		*pstapriv = &rtlpriv->stapriv;
	struct	mlme_priv	*pmlmepriv = &rtlpriv->mlmepriv;
	uint8_t *mybssid  = get_bssid(pmlmepriv);
	uint8_t *myhwaddr = rtlpriv->mac80211.mac_addr;
	uint8_t * sta_addr = NULL;
	int bmcast = is_multicast_ether_addr(pattrib->dst);




	if ((check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == true) ||
		(check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == true))
	{

		// filter packets that SA is myself or multicast or broadcast
		if (_rtw_memcmp(myhwaddr, pattrib->src, ETH_ALEN)){
			ret= _FAIL;
			goto exit;
		}

		if( (!_rtw_memcmp(myhwaddr, pattrib->dst, ETH_ALEN))	&& (!bmcast) ){
			ret= _FAIL;
			goto exit;
		}

		if( _rtw_memcmp(pattrib->bssid, "\x0\x0\x0\x0\x0\x0", ETH_ALEN) ||
		   _rtw_memcmp(mybssid, "\x0\x0\x0\x0\x0\x0", ETH_ALEN) ||
		   (!_rtw_memcmp(pattrib->bssid, mybssid, ETH_ALEN)) ) {
			ret= _FAIL;
			goto exit;
		}

		sta_addr = pattrib->src;

	} else if(check_fwstate(pmlmepriv, WIFI_STATION_STATE) == true)
	{
		{
			// For Station mode, sa and bssid should always be BSSID, and DA is my mac-address
			if(!_rtw_memcmp(pattrib->bssid, pattrib->src, ETH_ALEN) )
			{
				ret= _FAIL;
				goto exit;
		}

		sta_addr = pattrib->bssid;
		}

	}
	else if(check_fwstate(pmlmepriv, WIFI_AP_STATE) == true) {
		if (bmcast) {
			// For AP mode, if DA == MCAST, then BSSID should be also MCAST
			if (!is_multicast_ether_addr(pattrib->bssid)){
					ret= _FAIL;
					goto exit;
			}
		} else { // not mc-frame
			// For AP mode, if DA is non-MCAST, then it must be BSSID, and bssid == BSSID
			if(!_rtw_memcmp(pattrib->bssid, pattrib->dst, ETH_ALEN)) {
				ret= _FAIL;
				goto exit;
			}

			sta_addr = pattrib->src;
		}

	} else if(check_fwstate(pmlmepriv, WIFI_MP_STATE) == true) {
		memcpy(pattrib->dst, GetAddr1Ptr(ptr), ETH_ALEN);
		memcpy(pattrib->src, GetAddr2Ptr(ptr), ETH_ALEN);
		memcpy(pattrib->bssid, GetAddr3Ptr(ptr), ETH_ALEN);
		memcpy(pattrib->ra, pattrib->dst, ETH_ALEN);
		memcpy(pattrib->ta, pattrib->src, ETH_ALEN);

		sta_addr = mybssid;
	} else {
		ret  = _FAIL;
	}



	if(bmcast)
		*psta = rtw_get_bcmc_stainfo(rtlpriv);
	else
		*psta = rtw_get_stainfo(pstapriv, sta_addr); // get ap_info


	if (*psta == NULL) {
		ret= _FAIL;
		goto exit;
	}

exit:

	return ret;

}

int ap2sta_data_frame(
	struct rtl_priv *rtlpriv,
	struct recv_frame *precv_frame,
	struct sta_info**psta );
int ap2sta_data_frame(
	struct rtl_priv *rtlpriv,
	struct recv_frame *precv_frame,
	struct sta_info**psta )
{
	uint8_t *ptr = precv_frame->rx_data;
	struct rx_pkt_attrib *pattrib = & precv_frame->attrib;
	int ret = _SUCCESS;
	struct	sta_priv 		*pstapriv = &rtlpriv->stapriv;
	struct	mlme_priv	*pmlmepriv = &rtlpriv->mlmepriv;
	uint8_t *mybssid  = get_bssid(pmlmepriv);
	uint8_t *myhwaddr = rtlpriv->mac80211.mac_addr;
	int bmcast = is_multicast_ether_addr(pattrib->dst);



	if ((check_fwstate(pmlmepriv, WIFI_STATION_STATE) == true)
		&& (check_fwstate(pmlmepriv, _FW_LINKED) == true
			|| check_fwstate(pmlmepriv, _FW_UNDER_LINKING) == true	)
		)
	{

		/*  filter packets that SA is myself or multicast or broadcast */
		if (_rtw_memcmp(myhwaddr, pattrib->src, ETH_ALEN)){
			ret= _FAIL;
			goto exit;
		}

		/* da should be for me */
		if((!_rtw_memcmp(myhwaddr, pattrib->dst, ETH_ALEN))&& (!bmcast))
		{
			ret= _FAIL;
			goto exit;
		}


		/* check BSSID */
		if( _rtw_memcmp(pattrib->bssid, "\x0\x0\x0\x0\x0\x0", ETH_ALEN) ||
		     _rtw_memcmp(mybssid, "\x0\x0\x0\x0\x0\x0", ETH_ALEN) ||
		     (!_rtw_memcmp(pattrib->bssid, mybssid, ETH_ALEN)) )
		{
			if(!bmcast)
			{
				DBG_871X("issue_deauth to the nonassociated ap=" MAC_FMT " for the reason(7)\n", MAC_ARG(pattrib->bssid));
				issue_deauth(rtlpriv, pattrib->bssid, WLAN_REASON_CLASS3_FRAME_FROM_NONASSOC_STA);
			}

			ret= _FAIL;
			goto exit;
		}

		if(bmcast)
			*psta = rtw_get_bcmc_stainfo(rtlpriv);
		else
			*psta = rtw_get_stainfo(pstapriv, pattrib->bssid); // get ap_info

		if (*psta == NULL) {
			ret= _FAIL;
			goto exit;
		}

		if ((GetFrameSubType(ptr) & WIFI_QOS_DATA_TYPE) == WIFI_QOS_DATA_TYPE) {
		}

		if (GetFrameSubType(ptr) & BIT(6)) {
			/* No data, will not indicate to upper layer, temporily count it here */
			count_rx_stats(rtlpriv, precv_frame, *psta);
			ret = RTW_RX_HANDLED;
			goto exit;
		}

	} else if ((check_fwstate(pmlmepriv, WIFI_MP_STATE) == true)
		  && (check_fwstate(pmlmepriv, _FW_LINKED) == true)) {
		memcpy(pattrib->dst, GetAddr1Ptr(ptr), ETH_ALEN);
		memcpy(pattrib->src, GetAddr2Ptr(ptr), ETH_ALEN);
		memcpy(pattrib->bssid, GetAddr3Ptr(ptr), ETH_ALEN);
		memcpy(pattrib->ra, pattrib->dst, ETH_ALEN);
		memcpy(pattrib->ta, pattrib->src, ETH_ALEN);

		//
		memcpy(pattrib->bssid,  mybssid, ETH_ALEN);


		*psta = rtw_get_stainfo(pstapriv, pattrib->bssid); // get sta_info
		if (*psta == NULL) {
			ret= _FAIL;
			goto exit;
		}


	} else if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == true) {
		/* Special case */
		ret = RTW_RX_HANDLED;
		goto exit;
	} else {
		if(_rtw_memcmp(myhwaddr, pattrib->dst, ETH_ALEN)&& (!bmcast)) {
			*psta = rtw_get_stainfo(pstapriv, pattrib->bssid); // get sta_info
			if (*psta == NULL) {
				DBG_871X("issue_deauth to the ap=" MAC_FMT " for the reason(7)\n", MAC_ARG(pattrib->bssid));

				issue_deauth(rtlpriv, pattrib->bssid, WLAN_REASON_CLASS3_FRAME_FROM_NONASSOC_STA);
			}
		}

		ret = _FAIL;
	}

exit:



	return ret;

}

int sta2ap_data_frame(
	struct rtl_priv *rtlpriv,
	struct recv_frame *precv_frame,
	struct sta_info**psta );
int sta2ap_data_frame(
	struct rtl_priv *rtlpriv,
	struct recv_frame *precv_frame,
	struct sta_info**psta )
{
	uint8_t *ptr = precv_frame->rx_data;
	struct rx_pkt_attrib *pattrib = & precv_frame->attrib;
	struct	sta_priv 		*pstapriv = &rtlpriv->stapriv;
	struct	mlme_priv	*pmlmepriv = &rtlpriv->mlmepriv;
	unsigned char *mybssid  = get_bssid(pmlmepriv);
	int ret=_SUCCESS;



	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == true)
	{
		//For AP mode, RA=BSSID, TX=STA(SRC_ADDR), A3=DST_ADDR
		if(!_rtw_memcmp(pattrib->bssid, mybssid, ETH_ALEN))
		{
			ret= _FAIL;
			goto exit;
		}

		*psta = rtw_get_stainfo(pstapriv, pattrib->src);
		if (*psta == NULL)
		{
			DBG_871X("issue_deauth to sta=" MAC_FMT " for the reason(7)\n", MAC_ARG(pattrib->src));

			issue_deauth(rtlpriv, pattrib->src, WLAN_REASON_CLASS3_FRAME_FROM_NONASSOC_STA);

			ret = RTW_RX_HANDLED;
			goto exit;
		}

		process_pwrbit_data(rtlpriv, precv_frame);

		if ((GetFrameSubType(ptr) & WIFI_QOS_DATA_TYPE) == WIFI_QOS_DATA_TYPE) {
			process_wmmps_data(rtlpriv, precv_frame);
		}

		if (GetFrameSubType(ptr) & BIT(6)) {
			/* No data, will not indicate to upper layer, temporily count it here */
			count_rx_stats(rtlpriv, precv_frame, *psta);
			ret = RTW_RX_HANDLED;
			goto exit;
		}
	}
	else {
		uint8_t *myhwaddr = rtlpriv->mac80211.mac_addr;
		if (!_rtw_memcmp(pattrib->ra, myhwaddr, ETH_ALEN)) {
			ret = RTW_RX_HANDLED;
			goto exit;
		}
		DBG_871X("issue_deauth to sta=" MAC_FMT " for the reason(7)\n", MAC_ARG(pattrib->src));
		issue_deauth(rtlpriv, pattrib->src, WLAN_REASON_CLASS3_FRAME_FROM_NONASSOC_STA);
		ret = RTW_RX_HANDLED;
		goto exit;
	}

exit:



	return ret;

}

int validate_recv_ctrl_frame(struct rtl_priv *rtlpriv, struct recv_frame *precv_frame);
int validate_recv_ctrl_frame(struct rtl_priv *rtlpriv, struct recv_frame *precv_frame)
{
#ifdef CONFIG_AP_MODE
	struct rx_pkt_attrib *pattrib = &precv_frame->attrib;
	struct sta_priv *pstapriv = &rtlpriv->stapriv;
	uint8_t *pframe = precv_frame->rx_data;

	/* uint len = precv_frame->u.hdr.len; */

	/* DBG_871X("+validate_recv_ctrl_frame\n"); */

	if (GetFrameType(pframe) != WIFI_CTRL_TYPE)
	{
		return _FAIL;
	}

	/* receive the frames that ra(a1) is my address */
	if (!_rtw_memcmp(GetAddr1Ptr(pframe), rtlpriv->mac80211.mac_addr, ETH_ALEN))
	{
		return _FAIL;
	}

	/* only handle ps-poll */
	if (GetFrameSubType(pframe) == WIFI_PSPOLL)
	{
		u16 aid;
		uint8_t wmmps_ac=0;
		struct sta_info *psta=NULL;

		aid = GetAid(pframe);
		psta = rtw_get_stainfo(pstapriv, GetAddr2Ptr(pframe));

		if ((psta==NULL) || (psta->aid!=aid)) {
			return _FAIL;
		}

		/* for rx pkt statistics */
		psta->sta_stats.rx_ctrl_pkts++;

		switch(pattrib->priority) {
		case 1:
		case 2:
			wmmps_ac = psta->uapsd_bk&BIT(0);
			break;
		case 4:
		case 5:
			wmmps_ac = psta->uapsd_vi&BIT(0);
			break;
		case 6:
		case 7:
			wmmps_ac = psta->uapsd_vo&BIT(0);
			break;
		case 0:
		case 3:
		default:
			wmmps_ac = psta->uapsd_be&BIT(0);
			break;
		}

		if(wmmps_ac)
			return _FAIL;

		if(psta->state & WIFI_STA_ALIVE_CHK_STATE) {
			DBG_871X("%s alive check-rx ps-poll\n", __func__);
			psta->expire_to = pstapriv->expire_to;
			psta->state ^= WIFI_STA_ALIVE_CHK_STATE;
		}

		if ((psta->state&WIFI_SLEEP_STATE)
		   && (pstapriv->sta_dz_bitmap&BIT(psta->aid))) {
			struct list_head	*xmitframe_plist, *xmitframe_phead;
			struct xmit_frame *pxmitframe=NULL;
			struct xmit_priv *pxmitpriv = &rtlpriv->xmitpriv;

			/* spin_lock_bh(&psta->sleep_q.lock, &irqL);*/
			spin_lock_bh(&pxmitpriv->lock);

			xmitframe_phead = get_list_head(&psta->sleep_q);
			xmitframe_plist = get_next(xmitframe_phead);

			if ((rtw_end_of_queue_search(xmitframe_phead, xmitframe_plist)) == false) {
				pxmitframe = container_of(xmitframe_plist, struct xmit_frame, list);

				xmitframe_plist = get_next(xmitframe_plist);

				list_del_init(&pxmitframe->list);

				psta->sleepq_len--;

				if(psta->sleepq_len>0)
					pxmitframe->tx_attrib.mdata = 1;
                                else
					pxmitframe->tx_attrib.mdata = 0;

				pxmitframe->tx_attrib.triggered = 1;

	                        /*
	                         * DBG_871X("handling ps-poll, q_len=%d, tim=%x\n", psta->sleepq_len, pstapriv->tim_bitmap);
	                         */

				rtlpriv->cfg->ops->hal_xmitframe_enqueue(rtlpriv, pxmitframe);

				if (psta->sleepq_len==0) {
					pstapriv->tim_bitmap &= ~BIT(psta->aid);

					/*
					 * DBG_871X("after handling ps-poll, tim=%x\n", pstapriv->tim_bitmap);
					 */

					/* upate BCN for TIM IE */
					/* update_BCNTIM(rtlpriv); */
					update_beacon(rtlpriv, _TIM_IE_, NULL, false);
				}

				/* spin_unlock_bh(&psta->sleep_q.lock, &irqL); */
				spin_unlock_bh(&pxmitpriv->lock);
			} else {
				/* spin_unlock_bh(&psta->sleep_q.lock, &irqL); */
				spin_unlock_bh(&pxmitpriv->lock);

				/* DBG_871X("no buffered packets to xmit\n"); */
				if (pstapriv->tim_bitmap&BIT(psta->aid)) {
					if(psta->sleepq_len==0) {
						DBG_871X("no buffered packets to xmit\n");

						/* issue nulldata with More data bit = 0 to indicate we have no buffered packets */
						issue_nulldata(rtlpriv, psta->hwaddr, 0, 0, 0);
					} else {
						DBG_871X("error!psta->sleepq_len=%d\n", psta->sleepq_len);
						psta->sleepq_len=0;
					}

					pstapriv->tim_bitmap &= ~BIT(psta->aid);

					/* upate BCN for TIM IE */
					/* update_BCNTIM(rtlpriv); */
					update_beacon(rtlpriv, _TIM_IE_, NULL, false);
				}

			}

		}

	}

#endif

	return _FAIL;

}

struct recv_frame* recvframe_chk_defrag(struct rtl_priv *rtlpriv, struct recv_frame *precv_frame);
int validate_recv_mgnt_frame(struct rtl_priv *rtlpriv, struct recv_frame *precv_frame);
int validate_recv_mgnt_frame(struct rtl_priv *rtlpriv, struct recv_frame *precv_frame)
{
	/* struct mlme_priv *pmlmepriv = &rtlpriv->mlmepriv; */

	precv_frame = recvframe_chk_defrag(rtlpriv, precv_frame);
	if (precv_frame == NULL) {
		return _SUCCESS;
	}

	{
		//for rx pkt statistics
		struct sta_info *psta = rtw_get_stainfo(&rtlpriv->stapriv, GetAddr2Ptr(precv_frame->rx_data));
		if (psta) {
			psta->sta_stats.rx_mgnt_pkts++;
			if (GetFrameSubType(precv_frame->rx_data) == WIFI_BEACON)
				psta->sta_stats.rx_beacon_pkts++;
			else if (GetFrameSubType(precv_frame->rx_data) == WIFI_PROBEREQ)
				psta->sta_stats.rx_probereq_pkts++;
			else if (GetFrameSubType(precv_frame->rx_data) == WIFI_PROBERSP) {
				if (_rtw_memcmp(rtlpriv->mac80211.mac_addr, GetAddr1Ptr(precv_frame->rx_data), ETH_ALEN) == true)
					psta->sta_stats.rx_probersp_pkts++;
				else if (is_broadcast_mac_addr(GetAddr1Ptr(precv_frame->rx_data))
					|| is_multicast_mac_addr(GetAddr1Ptr(precv_frame->rx_data)))
					psta->sta_stats.rx_probersp_bm_pkts++;
				else
					psta->sta_stats.rx_probersp_uo_pkts++;
			}
		}
	}

	mgt_dispatcher(rtlpriv, precv_frame);

	return _SUCCESS;

}

int validate_recv_data_frame(struct rtl_priv *rtlpriv, struct recv_frame *precv_frame);
int validate_recv_data_frame(struct rtl_priv *rtlpriv, struct recv_frame *precv_frame)
{
	uint8_t bretry;
	uint8_t *psa, *pda, *pbssid;
	struct sta_info *psta = NULL;
	uint8_t *ptr = precv_frame->rx_data;
	struct rx_pkt_attrib	*pattrib = & precv_frame->attrib;
	struct sta_priv 	*pstapriv = &rtlpriv->stapriv;
	struct security_priv	*psecuritypriv = &rtlpriv->securitypriv;
	int ret = _SUCCESS;

	bretry = GetRetry(ptr);
	pda = get_da(ptr);
	psa = get_sa(ptr);
	pbssid = get_hdr_bssid(ptr);

	if(pbssid == NULL){
		ret= _FAIL;
		goto exit;
	}

	memcpy(pattrib->dst, pda, ETH_ALEN);
	memcpy(pattrib->src, psa, ETH_ALEN);

	memcpy(pattrib->bssid, pbssid, ETH_ALEN);

	switch(pattrib->to_fr_ds) {
	case 0:
		memcpy(pattrib->ra, pda, ETH_ALEN);
		memcpy(pattrib->ta, psa, ETH_ALEN);
		ret = sta2sta_data_frame(rtlpriv, precv_frame, &psta);
		break;

	case 1:
		memcpy(pattrib->ra, pda, ETH_ALEN);
		memcpy(pattrib->ta, pbssid, ETH_ALEN);
		ret = ap2sta_data_frame(rtlpriv, precv_frame, &psta);
		break;

	case 2:
		memcpy(pattrib->ra, pbssid, ETH_ALEN);
		memcpy(pattrib->ta, psa, ETH_ALEN);
		ret = sta2ap_data_frame(rtlpriv, precv_frame, &psta);
		break;

	case 3:
		memcpy(pattrib->ra, GetAddr1Ptr(ptr), ETH_ALEN);
		memcpy(pattrib->ta, GetAddr2Ptr(ptr), ETH_ALEN);
		ret =_FAIL;
		break;

	default:
		ret =_FAIL;
		break;
	}

	if(ret ==_FAIL){
		goto exit;
	} else if (ret == RTW_RX_HANDLED) {
		goto exit;
	}


	if(psta==NULL){
		ret= _FAIL;
		goto exit;
	}

	/*
	 * psta->rssi = prxcmd->rssi;
	 * psta->signal_quality= prxcmd->sq;
	 */

	precv_frame->psta = psta;


	pattrib->amsdu=0;
	pattrib->ack_policy = 0;
	/* parsing QC field */
	if (pattrib->qos == 1) {
		pattrib->priority = GetPriority((ptr + 24));
		pattrib->ack_policy = GetAckpolicy((ptr + 24));
		pattrib->amsdu = GetAMsdu((ptr + 24));
		pattrib->hdrlen = pattrib->to_fr_ds==3 ? 32 : 26;

		if(pattrib->priority!=0 && pattrib->priority!=3) {
			rtlpriv->dm.is_any_nonbepkts = true;
		}
	} else {
		pattrib->priority=0;
		pattrib->hdrlen = pattrib->to_fr_ds==3 ? 30 : 24;
	}


	if(pattrib->order) {//HT-CTRL 11n
		pattrib->hdrlen += 4;
	}

	precv_frame->preorder_ctrl = &psta->recvreorder_ctrl[pattrib->priority];

	/* decache, drop duplicate recv packets */
	if(recv_decache(precv_frame, bretry, &psta->sta_recvpriv.rxcache) == _FAIL) {
		ret= _FAIL;
		goto exit;
	}

	if(pattrib->privacy){
		GET_ENCRY_ALGO(psecuritypriv, psta, pattrib->encrypt, is_multicast_ether_addr(pattrib->ra));

		SET_ICE_IV_LEN(pattrib->iv_len, pattrib->icv_len, pattrib->encrypt);
	}
	else
	{
		pattrib->encrypt = 0;
		pattrib->iv_len = pattrib->icv_len = 0;
	}

exit:

	return ret;
}

int validate_recv_frame(struct rtl_priv *rtlpriv, struct recv_frame *precv_frame);
int validate_recv_frame(struct rtl_priv *rtlpriv, struct recv_frame *precv_frame)
{
	/* shall check frame subtype, to / from ds, da, bssid */

	/* then call check if rx seq/frag. duplicated. */

	uint8_t type;
	uint8_t subtype;
	int retval = _SUCCESS;

	struct rx_pkt_attrib *pattrib = & precv_frame->attrib;

	uint8_t *ptr = precv_frame->rx_data;
	uint8_t  ver =(unsigned char) (*ptr)&0x3 ;




	/* add version chk */
	if (ver != 0){
		retval= _FAIL;
		goto exit;
	}

	type =  GetFrameType(ptr);
	subtype = GetFrameSubType(ptr); //bit(7)~bit(2)

	pattrib->to_fr_ds = get_tofr_ds(ptr);

	pattrib->frag_num = GetFragNum(ptr);
	pattrib->seq_num = GetSequence(ptr);

	pattrib->pw_save = GetPwrMgt(ptr);
	pattrib->mfrag = GetMFrag(ptr);
	pattrib->mdata = GetMData(ptr);
	pattrib->privacy = GetPrivacy(ptr);
	pattrib->order = GetOrder(ptr);

	switch (type) {
	case WIFI_MGT_TYPE: /* mgnt */
		retval = validate_recv_mgnt_frame(rtlpriv, precv_frame);
		if (retval == _FAIL)
		{
			;
		}
		retval = _FAIL; /* only data frame return _SUCCESS */
		break;
	case WIFI_CTRL_TYPE: //ctrl
		retval = validate_recv_ctrl_frame(rtlpriv, precv_frame);
		if (retval == _FAIL)
		{
			;
		}
		retval = _FAIL; /* only data frame return _SUCCESS */
		break;
	case WIFI_DATA_TYPE: /* data */
		pattrib->qos = (subtype & BIT(7))? 1:0;
		retval = validate_recv_data_frame(rtlpriv, precv_frame);
		if (retval == _FAIL)
		{
			struct recv_priv *precvpriv = &rtlpriv->recvpriv;
			//RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("validate_recv_data_frame fail\n"));
			precvpriv->rx_drop++;
		}
		break;
	default:
		retval = _FAIL;
		break;
	}

exit:

	return retval;
}


/* remove the wlanhdr and add the eth_hdr */
#if 1

int wlanhdr_to_ethhdr ( struct recv_frame *precvframe);
int wlanhdr_to_ethhdr ( struct recv_frame *precvframe)
{
	int rmv_len;
	u16 eth_type, len;
	uint8_t	bsnaphdr;
	uint8_t	*psnap_type;
	struct ieee80211_snap_hdr *psnap;

	int ret=_SUCCESS;
	struct rtl_priv *rtlpriv =precvframe->rtlpriv;
	struct mlme_priv *pmlmepriv = &rtlpriv->mlmepriv;

	uint8_t	*ptr = get_recvframe_data(precvframe) ; /*  point to frame_ctrl field */
	struct rx_pkt_attrib *pattrib = & precvframe->attrib;

	if (pattrib->encrypt) {
		recvframe_pull_tail(precvframe, pattrib->icv_len);
	}

	psnap=(struct ieee80211_snap_hdr	*)(ptr+pattrib->hdrlen + pattrib->iv_len);
	psnap_type=ptr+pattrib->hdrlen + pattrib->iv_len+SNAP_SIZE;

	/* convert hdr + possible LLC headers into Ethernet header */
	/* eth_type = (psnap_type[0] << 8) | psnap_type[1]; */

	if((_rtw_memcmp(psnap, rtw_rfc1042_header, SNAP_SIZE) &&
		(_rtw_memcmp(psnap_type, SNAP_ETH_TYPE_IPX, 2) == false) &&
		(_rtw_memcmp(psnap_type, SNAP_ETH_TYPE_APPLETALK_AARP, 2)==false) )||
		/* eth_type != ETH_P_AARP && eth_type != ETH_P_IPX) || */
		 _rtw_memcmp(psnap, rtw_bridge_tunnel_header, SNAP_SIZE)){
		 /* remove RFC1042 or Bridge-Tunnel encapsulation and replace EtherType */
		bsnaphdr = true;
	}
	else {
		/* Leave Ethernet header part of hdr and full payload */
		bsnaphdr = false;
	}

	rmv_len = pattrib->hdrlen + pattrib->iv_len +(bsnaphdr?SNAP_SIZE:0);
	len = precvframe->len - rmv_len;

	memcpy(&eth_type, ptr+rmv_len, 2);
	eth_type= ntohs((unsigned short )eth_type); //pattrib->ether_type
	pattrib->eth_type = eth_type;

	if ((check_fwstate(pmlmepriv, WIFI_MP_STATE) == true)) {
		ptr += rmv_len ;
		*ptr = 0x87;
		*(ptr+1) = 0x12;

		eth_type = 0x8712;
		/* append rx status for mp test packets */
		ptr = recvframe_pull(precvframe, (rmv_len-sizeof(struct ethhdr)+2)-24);
		memcpy(ptr, get_rxmem(precvframe), 24);
		ptr+=24;
	} else {
		ptr = recvframe_pull(precvframe, (rmv_len-sizeof(struct ethhdr)+ (bsnaphdr?2:0)));
	}

	memcpy(ptr, pattrib->dst, ETH_ALEN);
	memcpy(ptr+ETH_ALEN, pattrib->src, ETH_ALEN);

	if(!bsnaphdr) {
		len = htons(len);
		memcpy(ptr+12, &len, 2);
	}


	return ret;

}

#else

int wlanhdr_to_ethhdr ( struct recv_frame *precvframe)
{
	int rmv_len;
	u16 eth_type;
	uint8_t	bsnaphdr;
	uint8_t	*psnap_type;
	struct ieee80211_snap_hdr	*psnap;

	int ret=_SUCCESS;
	struct rtl_priv	*rtlpriv =precvframe->u.hdr.rtlpriv;
	struct	mlme_priv	*pmlmepriv = &rtlpriv->mlmepriv;

	uint8_t * ptr = get_recvframe_data(precvframe) ; /* point to frame_ctrl field */
	struct rx_pkt_attrib *pattrib = & precvframe->u.hdr.attrib;
	struct _vlan *pvlan = NULL;



	psnap=(struct ieee80211_snap_hdr *)(ptr+pattrib->hdrlen + pattrib->iv_len);
	psnap_type=ptr+pattrib->hdrlen + pattrib->iv_len+SNAP_SIZE;
	if (psnap->dsap==0xaa && psnap->ssap==0xaa && psnap->ctrl==0x03) {
		if (_rtw_memcmp(psnap->oui, oui_rfc1042, WLAN_IEEE_OUI_LEN))
			bsnaphdr=true;		/*wlan_pkt_format = WLAN_PKT_FORMAT_SNAP_RFC1042; */
		else if (_rtw_memcmp(psnap->oui, SNAP_HDR_APPLETALK_DDP, WLAN_IEEE_OUI_LEN) &&
			_rtw_memcmp(psnap_type, SNAP_ETH_TYPE_APPLETALK_DDP, 2) )
			bsnaphdr=true;		/* wlan_pkt_format = WLAN_PKT_FORMAT_APPLETALK; */
		else if (_rtw_memcmp( psnap->oui, oui_8021h, WLAN_IEEE_OUI_LEN))
			bsnaphdr=true;		/*wlan_pkt_format = WLAN_PKT_FORMAT_SNAP_TUNNEL; */
		else {
			ret= _FAIL;
			goto exit;
		}

	} else
		bsnaphdr=false;	/* wlan_pkt_format = WLAN_PKT_FORMAT_OTHERS; */

	rmv_len = pattrib->hdrlen + pattrib->iv_len +(bsnaphdr?SNAP_SIZE:0);

	if (check_fwstate(pmlmepriv, WIFI_MP_STATE) == true) {
		ptr += rmv_len ;
		*ptr = 0x87;
		*(ptr+1) = 0x12;

		/* back to original pointer */
		ptr -= rmv_len;
	}

	ptr += rmv_len ;

	memcpy(&eth_type, ptr, 2);
	eth_type= ntohs((unsigned short )eth_type); //pattrib->ether_type
	ptr +=2;

	if(pattrib->encrypt){
		recvframe_pull_tail(precvframe, pattrib->icv_len);
	}

	if(eth_type == 0x8100) /* vlan */
	{
		pvlan = (struct _vlan *) ptr;

		/*
		 * eth_type = get_vlan_encap_proto(pvlan);
		 * eth_type = pvlan->h_vlan_encapsulated_proto;//?
		 */
		rmv_len += 4;
		ptr+=4;
	}

	if(eth_type==0x0800) { /* ip */
		/*
		 * struct iphdr*  piphdr = (struct iphdr*) ptr;
		 * __uint8_t tos = (unsigned char)(pattrib->priority & 0xff);
		 *
		 * piphdr->tos = tos;
		 *
		 * if (piphdr->protocol == 0x06) {
		 * 	RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("@@@===recv tcp len:%d @@@===\n", precvframe->u.hdr.len));
		 * }
		 */

	} else if(eth_type==0x8712) { 	/* append rx status for mp test packets */
		/*
		 * ptr -= 16;
		 * memcpy(ptr, get_rxmem(precvframe), 16);
		 */
	} else {
	}

	if(eth_type==0x8712) { 	/*  append rx status for mp test packets */
		ptr = recvframe_pull(precvframe, (rmv_len-sizeof(struct ethhdr)+2)-24);
		memcpy(ptr, get_rxmem(precvframe), 24);
		ptr+=24;
	} else
		ptr = recvframe_pull(precvframe, (rmv_len-sizeof(struct ethhdr)+2));

	memcpy(ptr, pattrib->dst, ETH_ALEN);
	memcpy(ptr+ETH_ALEN, pattrib->src, ETH_ALEN);

	eth_type = htons((unsigned short)eth_type) ;
	memcpy(ptr+12, &eth_type, 2);

exit:

	return ret;
}
#endif

/* perform defrag */

struct recv_frame *recvframe_defrag(struct rtl_priv *rtlpriv,struct __queue *defrag_q);
struct recv_frame *recvframe_defrag(struct rtl_priv *rtlpriv,struct __queue *defrag_q)
{
	struct list_head	 *plist, *phead;
	uint8_t	*data,wlanhdr_offset;
	uint8_t	curfragnum;
	struct recv_frame* prframe, *pnextrframe;
	struct __queue	*pfree_recv_queue;


	curfragnum=0;
	pfree_recv_queue=&rtlpriv->recvpriv.free_recv_queue;

	phead = get_list_head(defrag_q);
	plist = get_next(phead);
	prframe = container_of(plist, struct recv_frame, list);
	list_del_init(&(prframe->list));

	if (curfragnum != prframe->attrib.frag_num) {
		/*
		 * the first fragment number must be 0
		 * free the whole queue
		 */
		rtw_free_recvframe(prframe, pfree_recv_queue);
		rtw_free_recvframe_queue(defrag_q, pfree_recv_queue);

		return NULL;
	}


	curfragnum++;

	plist= get_list_head(defrag_q);

	plist = get_next(plist);

	data=get_recvframe_data(prframe);

	while (rtw_end_of_queue_search(phead, plist) == false) {
		pnextrframe = container_of(plist, struct recv_frame , list);

		/* check the fragment sequence  (2nd ~n fragment frame) */

		if(curfragnum!=pnextrframe->attrib.frag_num) {
			/*
			 * the fragment number must be increasing  (after decache)
			 * release the defrag_q & prframe
			 */
			rtw_free_recvframe(prframe, pfree_recv_queue);
			rtw_free_recvframe_queue(defrag_q, pfree_recv_queue);
			return NULL;
		}

		curfragnum++;

		/*
		 * copy the 2nd~n fragment frame's payload to the first fragment
		 * get the 2nd~last fragment frame's payload
		 */

		wlanhdr_offset = pnextrframe->attrib.hdrlen + pnextrframe->attrib.iv_len;

		recvframe_pull(pnextrframe, wlanhdr_offset);

		/*
		 * append  to first fragment frame's tail (if privacy frame, pull the ICV)
		 */
		recvframe_pull_tail(prframe, prframe->attrib.icv_len);

		/* memcpy */
		memcpy(prframe->rx_tail, pnextrframe->rx_data, pnextrframe->len);

		recvframe_put(prframe, pnextrframe->len);

		prframe->attrib.icv_len=pnextrframe->attrib.icv_len;
		plist = get_next(plist);

	};

	/* free the defrag_q queue and return the prframe */
	rtw_free_recvframe_queue(defrag_q, pfree_recv_queue);

	return prframe;
}

/* check if need to defrag, if needed queue the frame to defrag_q */
struct recv_frame* recvframe_chk_defrag(struct rtl_priv *rtlpriv, struct recv_frame *precv_frame)
{
	uint8_t	ismfrag;
	uint8_t	fragnum;
	uint8_t	*psta_addr;
	struct sta_info *psta;
	struct sta_priv *pstapriv;
	struct list_head *phead;
	struct recv_frame *prtnframe = NULL;
	struct __queue *pfree_recv_queue, *pdefrag_q;

	pstapriv = &rtlpriv->stapriv;

	pfree_recv_queue = &rtlpriv->recvpriv.free_recv_queue;

	/* need to define struct of wlan header frame ctrl */
	ismfrag = precv_frame->attrib.mfrag;
	fragnum = precv_frame->attrib.frag_num;

	psta_addr = precv_frame->attrib.ta;
	psta = rtw_get_stainfo(pstapriv, psta_addr);
	if (psta == NULL) {
		uint8_t type = GetFrameType(precv_frame->rx_data);
		if (type != WIFI_DATA_TYPE) {
			psta = rtw_get_bcmc_stainfo(rtlpriv);
			pdefrag_q = &psta->sta_recvpriv.defrag_q;
		} else
			pdefrag_q = NULL;
	} else
		pdefrag_q = &psta->sta_recvpriv.defrag_q;

	if ((ismfrag==0) && (fragnum==0)) {
		prtnframe = precv_frame;	/* isn't a fragment frame */
	}

	if (ismfrag==1) {
		/*
		 * 0~(n-1) fragment frame
		 * enqueue to defraf_g
		 */

		if (pdefrag_q != NULL) {
			if (fragnum == 0) {
				/* the first fragment */
				if(!list_empty(&pdefrag_q->list)) {
					/* free current defrag_q */
					rtw_free_recvframe_queue(pdefrag_q, pfree_recv_queue);
				}
			}


			/* Then enqueue the 0~(n-1) fragment into the defrag_q */

			/* _rtw_spinlock(&pdefrag_q->lock); */
			phead = get_list_head(pdefrag_q);
			list_add_tail(&precv_frame->list, phead);
			/* spin_unlock(&pdefrag_q->lock); */

			prtnframe=NULL;

		} else 	{
			/* can't find this ta's defrag_queue, so free this recv_frame */
			rtw_free_recvframe(precv_frame, pfree_recv_queue);
			prtnframe=NULL;
		}

	}

	if((ismfrag==0)&&(fragnum!=0)) {
		/*
		 * the last fragment frame
		 * enqueue the last fragment
		 */

		if (pdefrag_q != NULL) {
			/* _rtw_spinlock(&pdefrag_q->lock); */
			phead = get_list_head(pdefrag_q);
			list_add_tail(&precv_frame->list,phead);
			/* spin_unlock(&pdefrag_q->lock); */

			/* call recvframe_defrag to defrag */
			precv_frame = recvframe_defrag(rtlpriv, pdefrag_q);
			prtnframe=precv_frame;

		} else {
			/* can't find this ta's defrag_queue, so free this recv_frame */
			rtw_free_recvframe(precv_frame, pfree_recv_queue);
			prtnframe=NULL;
		}

	}

	if((prtnframe!=NULL)&&(prtnframe->attrib.privacy)) {
		/* after defrag we must check tkip mic code */
		if(recvframe_chkmic(rtlpriv,  prtnframe)==_FAIL)
		{
			rtw_free_recvframe(prtnframe,pfree_recv_queue);
			prtnframe=NULL;
		}
	}

	return prtnframe;
}

int amsdu_to_msdu(struct rtl_priv *rtlpriv, struct recv_frame *prframe)
{
	int	a_len, padding_len;
	u16	nSubframe_Length;
	uint8_t	nr_subframes, i;
	uint8_t	*pdata;
	struct sk_buff *sub_pkt,*subframes[MAX_SUBFRAME_COUNT];
	struct recv_priv *precvpriv = &rtlpriv->recvpriv;
	struct __queue *pfree_recv_queue = &(precvpriv->free_recv_queue);
	int	ret = _SUCCESS;

	nr_subframes = 0;

	recvframe_pull(prframe, prframe->attrib.hdrlen);

	if (prframe->attrib.iv_len >0) {
		recvframe_pull(prframe, prframe->attrib.iv_len);
	}

	a_len = prframe->len;

	pdata = prframe->rx_data;

	while (a_len > ETH_HLEN) {
		/* Offset 12 denote 2 mac address */
		nSubframe_Length = RTW_GET_BE16(pdata + 12);

		if (a_len < (ETHERNET_HEADER_SIZE + nSubframe_Length) ) {
			DBG_871X("nRemain_Length is %d and nSubframe_Length is : %d\n",a_len,nSubframe_Length);
			break;
		}

		sub_pkt = rtw_os_alloc_msdu_pkt(prframe, nSubframe_Length, pdata);
		if (sub_pkt == NULL) {
			DBG_871X("%s(): allocate sub packet fail !!!\n",__FUNCTION__);
			break;
		}

		/* move the data point to data content */
		pdata += ETH_HLEN;
		a_len -= ETH_HLEN;

		subframes[nr_subframes++] = sub_pkt;

		if (nr_subframes >= MAX_SUBFRAME_COUNT) {
			DBG_871X("ParseSubframe(): Too many Subframes! Packets dropped!\n");
			break;
		}

		pdata += nSubframe_Length;
		a_len -= nSubframe_Length;
		if (a_len != 0) {
			padding_len = 4 - ((nSubframe_Length + ETH_HLEN) & (4-1));
			if (padding_len == 4) {
				padding_len = 0;
			}

			if (a_len < padding_len) {
				DBG_871X("ParseSubframe(): a_len < padding_len !\n");
				break;
			}
			pdata += padding_len;
			a_len -= padding_len;
		}
	}

	for (i = 0; i < nr_subframes; i++){
		sub_pkt = subframes[i];

		/* Indicat the packets to upper layer */
		if (sub_pkt) {
			rtw_os_recv_indicate_pkt(rtlpriv, sub_pkt, &prframe->attrib);
		}
	}

	prframe->len = 0;
	rtw_free_recvframe(prframe, pfree_recv_queue);//free this recv_frame

	return ret;
}

int check_indicate_seq(struct recv_reorder_ctrl *preorder_ctrl, u16 seq_num);
int check_indicate_seq(struct recv_reorder_ctrl *preorder_ctrl, u16 seq_num)
{
	uint8_t	wsize = preorder_ctrl->wsize_b;
	u16	wend = (preorder_ctrl->indicate_seq + wsize -1) & 0xFFF;//% 4096;

	/*  Rx Reorder initialize condition. */
	if (preorder_ctrl->indicate_seq == 0xFFFF) {
		preorder_ctrl->indicate_seq = seq_num;

		/*
		 * DbgPrint("check_indicate_seq, 1st->indicate_seq=%d\n", precvpriv->indicate_seq);
		 */
	}

	/*
	 * DbgPrint("enter->check_indicate_seq(): IndicateSeq: %d, NewSeq: %d\n", precvpriv->indicate_seq, seq_num);
	 */

	/* Drop out the packet which SeqNum is smaller than WinStart */
	if( SN_LESS(seq_num, preorder_ctrl->indicate_seq) ) {
		/*
		 * RT_TRACE(COMP_RX_REORDER, DBG_LOUD, ("CheckRxTsIndicateSeq(): Packet Drop! IndicateSeq: %d, NewSeq: %d\n", pTS->RxIndicateSeq, NewSeqNum));
		 * DbgPrint("CheckRxTsIndicateSeq(): Packet Drop! IndicateSeq: %d, NewSeq: %d\n", precvpriv->indicate_seq, seq_num);
		 */
		return false;
	}

	/*
	 * Sliding window manipulation. Conditions includes:
	 * 1. Incoming SeqNum is equal to WinStart =>Window shift 1
	 * 2. Incoming SeqNum is larger than the WinEnd => Window shift N
	 */

	if (SN_EQUAL(seq_num, preorder_ctrl->indicate_seq)) {
		preorder_ctrl->indicate_seq = (preorder_ctrl->indicate_seq + 1) & 0xFFF;
	} else if(SN_LESS(wend, seq_num)) {
		/*
		 * RT_TRACE(COMP_RX_REORDER, DBG_LOUD, ("CheckRxTsIndicateSeq(): Window Shift! IndicateSeq: %d, NewSeq: %d\n", pTS->RxIndicateSeq, NewSeqNum));
		 * DbgPrint("CheckRxTsIndicateSeq(): Window Shift! IndicateSeq: %d, NewSeq: %d\n", precvpriv->indicate_seq, seq_num);
		 */

		/* boundary situation, when seq_num cross 0xFFF */
		if(seq_num >= (wsize - 1))
			preorder_ctrl->indicate_seq = seq_num + 1 -wsize;
		else
			preorder_ctrl->indicate_seq = 0xFFF - (wsize - (seq_num + 1)) + 1;

	}

	/*
	 * DbgPrint("exit->check_indicate_seq(): IndicateSeq: %d, NewSeq: %d\n", precvpriv->indicate_seq, seq_num);
	 */

	return true;
}

int enqueue_reorder_recvframe(struct recv_reorder_ctrl *preorder_ctrl, struct recv_frame *prframe);
int enqueue_reorder_recvframe(struct recv_reorder_ctrl *preorder_ctrl, struct recv_frame *prframe)
{
	struct rx_pkt_attrib *pattrib = &prframe->attrib;
	struct __queue *ppending_recvframe_queue = &preorder_ctrl->pending_recvframe_queue;
	struct list_head	*phead, *plist;
	struct recv_frame *pnextrframe;
	struct rx_pkt_attrib *pnextattrib;

	/* DbgPrint("+enqueue_reorder_recvframe()\n"); */

	/*
	 * spin_lock_irqsave(&ppending_recvframe_queue->lock, &irql);
	 * _rtw_spinlock_ex(&ppending_recvframe_queue->lock);
	 */


	phead = get_list_head(ppending_recvframe_queue);
	plist = get_next(phead);

	while (rtw_end_of_queue_search(phead, plist) == false) {
		pnextrframe = container_of(plist, struct recv_frame, list);
		pnextattrib = &pnextrframe->attrib;

		if (SN_LESS(pnextattrib->seq_num, pattrib->seq_num)) {
			plist = get_next(plist);
		} else if( SN_EQUAL(pnextattrib->seq_num, pattrib->seq_num)) {
			/*
			 * Duplicate entry is found!! Do not insert current entry.
			 * RT_TRACE(COMP_RX_REORDER, DBG_TRACE, ("InsertRxReorderList(): Duplicate packet is dropped!! IndicateSeq: %d, NewSeq: %d\n", pTS->RxIndicateSeq, SeqNum));
			 */

			/*
			 * spin_unlock_irqrestore(&ppending_recvframe_queue->lock, &irql);
			 */

			return false;
		} else {
			break;
		}

		/* DbgPrint("enqueue_reorder_recvframe():while\n"); */
	}


	/*
	 * spin_lock_irqsave(&ppending_recvframe_queue->lock, &irql);
	 * _rtw_spinlock_ex(&ppending_recvframe_queue->lock);
	 */

	list_del_init(&(prframe->list));

	list_add_tail(&(prframe->list), plist);

	/*
	 * _rtw_spinunlock_ex(&ppending_recvframe_queue->lock);
	 *spin_unlock_irqrestore(&ppending_recvframe_queue->lock, &irql);
	 */


	/*
	 * RT_TRACE(COMP_RX_REORDER, DBG_TRACE, ("InsertRxReorderList(): Pkt insert into buffer!! IndicateSeq: %d, NewSeq: %d\n", pTS->RxIndicateSeq, SeqNum));
	 */
	return true;

}

int recv_indicatepkts_in_order(struct rtl_priv *rtlpriv, struct recv_reorder_ctrl *preorder_ctrl, int bforced);
int recv_indicatepkts_in_order(struct rtl_priv *rtlpriv, struct recv_reorder_ctrl *preorder_ctrl, int bforced)
{
	/* _irqL irql; */
	/* uint8_t bcancelled; */
	struct list_head	*phead, *plist;
	struct recv_frame *prframe;
	struct rx_pkt_attrib *pattrib;
	/* uint8_t index = 0; */
	int bPktInBuf = false;
	struct recv_priv *precvpriv = &rtlpriv->recvpriv;
	struct __queue *ppending_recvframe_queue = &preorder_ctrl->pending_recvframe_queue;

	/* DbgPrint("+recv_indicatepkts_in_order\n"); */

	/*
	 * spin_lock_irqsave(&ppending_recvframe_queue->lock, &irql);
	 * _rtw_spinlock_ex(&ppending_recvframe_queue->lock);
	 */

	phead = 	get_list_head(ppending_recvframe_queue);
	plist = get_next(phead);

	/* Handling some condition for forced indicate case. */
	if (bforced==true) {
		if(list_empty(phead)) {
			/*
			 * spin_unlock_irqrestore(&ppending_recvframe_queue->lock, &irql);
			 * _rtw_spinunlock_ex(&ppending_recvframe_queue->lock);
			 */
			return true;
		}

		 prframe = container_of(plist, struct recv_frame, list);
	        pattrib = &prframe->attrib;
		preorder_ctrl->indicate_seq = pattrib->seq_num;
	}

	/*
	 *  Prepare indication list and indication.
	 * Check if there is any packet need indicate.
	 */

	while (!list_empty(phead)) {

		prframe = container_of(plist, struct recv_frame, list);
		pattrib = &prframe->attrib;

		if(!SN_LESS(preorder_ctrl->indicate_seq, pattrib->seq_num)) {
			plist = get_next(plist);
			list_del_init(&(prframe->list));

			if(SN_EQUAL(preorder_ctrl->indicate_seq, pattrib->seq_num)) {
				preorder_ctrl->indicate_seq = (preorder_ctrl->indicate_seq + 1) & 0xFFF;
			}

			/*
			 * Set this as a lock to make sure that only one thread is indicating packet.
			 * pTS->RxIndicateState = RXTS_INDICATE_PROCESSING;
			 */

			/*
			 * Indicate packets
			 * RT_ASSERT((index<=REORDER_WIN_SIZE), ("RxReorderIndicatePacket(): Rx Reorder buffer full!! \n"));
			 */

			/*
			 * indicate this recv_frame
			 * DbgPrint("recv_indicatepkts_in_order, indicate_seq=%d, seq_num=%d\n", precvpriv->indicate_seq, pattrib->seq_num);
			 */

			if (!pattrib->amsdu) {
				/* DBG_871X("recv_indicatepkts_in_order, amsdu!=1, indicate_seq=%d, seq_num=%d\n", preorder_ctrl->indicate_seq, pattrib->seq_num); */

				if ((rtlpriv->bDriverStopped == false)
				  && (rtlpriv->bSurpriseRemoved == false)) {
					rtw_recv_indicatepkt(rtlpriv, prframe);//indicate this recv_frame

				}
			} else if (pattrib->amsdu == 1) {
				if (amsdu_to_msdu(rtlpriv, prframe) != _SUCCESS) {
					rtw_free_recvframe(prframe, &precvpriv->free_recv_queue);
				}
			} else 	{
				/* error condition; */
			}


			/* Update local variables. */
			bPktInBuf = false;

		} else {
			bPktInBuf = true;
			break;
		}

		/* DbgPrint("recv_indicatepkts_in_order():while\n"); */

	}

	/*
	 * _rtw_spinunlock_ex(&ppending_recvframe_queue->lock);
	 * spin_unlock_irqrestore(&ppending_recvframe_queue->lock, &irql);
	 */


/*
	//Release the indication lock and set to new indication step.
	if(bPktInBuf)
	{
		// Set new pending timer.
		//pTS->RxIndicateState = RXTS_INDICATE_REORDER;
		//PlatformSetTimer(rtlpriv, &pTS->RxPktPendingTimer, pHTInfo->RxReorderPendingTime);
		//DBG_871X("_set_timer(&preorder_ctrl->reordering_ctrl_timer, REORDER_WAIT_TIME)\n");
		_set_timer(&preorder_ctrl->reordering_ctrl_timer, REORDER_WAIT_TIME);
	}
	else
	{
		//pTS->RxIndicateState = RXTS_INDICATE_IDLE;
	}
*/
	/* spin_unlock_irqrestore(&ppending_recvframe_queue->lock, &irql); */

	/* return true; */
	return bPktInBuf;

}

int recv_indicatepkt_reorder(struct rtl_priv *rtlpriv, struct recv_frame *prframe);
int recv_indicatepkt_reorder(struct rtl_priv *rtlpriv, struct recv_frame *prframe)
{
	int retval = _SUCCESS;
	struct rx_pkt_attrib *pattrib = &prframe->attrib;
	struct recv_reorder_ctrl *preorder_ctrl = prframe->preorder_ctrl;
	struct __queue *ppending_recvframe_queue = &preorder_ctrl->pending_recvframe_queue;

	if(!pattrib->amsdu)
	{
		//s1.
		wlanhdr_to_ethhdr(prframe);

		if ((pattrib->qos!=1) /*|| pattrib->priority!=0 || is_multicast_ether_addr(pattrib->ra)*/
			|| (pattrib->eth_type==0x0806) || (pattrib->ack_policy!=0))
		{
			if ((rtlpriv->bDriverStopped == false) &&
			    (rtlpriv->bSurpriseRemoved == false))
			{
				rtw_recv_indicatepkt(rtlpriv, prframe);
				return _SUCCESS;

			}


			return _FAIL;

		}

		if (preorder_ctrl->enable == false)
		{
			//indicate this recv_frame
			preorder_ctrl->indicate_seq = pattrib->seq_num;

			rtw_recv_indicatepkt(rtlpriv, prframe);

			preorder_ctrl->indicate_seq = (preorder_ctrl->indicate_seq + 1)%4096;

			return _SUCCESS;
		}

#ifndef CONFIG_RECV_REORDERING_CTRL
		//indicate this recv_frame
		rtw_recv_indicatepkt(rtlpriv, prframe);
		return _SUCCESS;
#endif

	}
	else if(pattrib->amsdu==1) //temp filter -> means didn't support A-MSDUs in a A-MPDU
	{
		if (preorder_ctrl->enable == false)
		{
			preorder_ctrl->indicate_seq = pattrib->seq_num;

			retval = amsdu_to_msdu(rtlpriv, prframe);

			preorder_ctrl->indicate_seq = (preorder_ctrl->indicate_seq + 1)%4096;

			if(retval != _SUCCESS){
			}

			return retval;
		}
	}
	else
	{

	}

	spin_lock_bh(&ppending_recvframe_queue->lock);

	//s2. check if winstart_b(indicate_seq) needs to been updated
	if(!check_indicate_seq(preorder_ctrl, pattrib->seq_num))
	{
		//pHTInfo->RxReorderDropCounter++;
		//ReturnRFDList(rtlpriv, pRfd);
		//RT_TRACE(COMP_RX_REORDER, DBG_TRACE, ("RxReorderIndicatePacket() ==> Packet Drop!!\n"));
		//spin_unlock_irqrestore(&ppending_recvframe_queue->lock, &irql);
		//return _FAIL;

		goto _err_exit;
	}


	//s3. Insert all packet into Reorder Queue to maintain its ordering.
	if(!enqueue_reorder_recvframe(preorder_ctrl, prframe))
	{
		//DbgPrint("recv_indicatepkt_reorder, enqueue_reorder_recvframe fail!\n");
		//spin_unlock_irqrestore(&ppending_recvframe_queue->lock, &irql);
		//return _FAIL;
		goto _err_exit;
	}


	//s4.
	// Indication process.
	// After Packet dropping and Sliding Window shifting as above, we can now just indicate the packets
	// with the SeqNum smaller than latest WinStart and buffer other packets.
	//
	// For Rx Reorder condition:
	// 1. All packets with SeqNum smaller than WinStart => Indicate
	// 2. All packets with SeqNum larger than or equal to WinStart => Buffer it.
	//

	//recv_indicatepkts_in_order(rtlpriv, preorder_ctrl, true);
	if(recv_indicatepkts_in_order(rtlpriv, preorder_ctrl, false)==true)
	{
		_set_timer(&preorder_ctrl->reordering_ctrl_timer, REORDER_WAIT_TIME);
		spin_unlock_bh(&ppending_recvframe_queue->lock);
	}
	else
	{
		spin_unlock_bh(&ppending_recvframe_queue->lock);
		del_timer_sync_ex(&preorder_ctrl->reordering_ctrl_timer);
	}


_success_exit:

	return _SUCCESS;

_err_exit:

        spin_unlock_bh(&ppending_recvframe_queue->lock);

	return _FAIL;
}


void rtw_reordering_ctrl_timeout_handler(void *pcontext)
{
	struct recv_reorder_ctrl *preorder_ctrl = (struct recv_reorder_ctrl *)pcontext;
	struct rtl_priv *rtlpriv = preorder_ctrl->rtlpriv;
	struct __queue *ppending_recvframe_queue = &preorder_ctrl->pending_recvframe_queue;


	if(rtlpriv->bDriverStopped ||rtlpriv->bSurpriseRemoved)
	{
		return;
	}

	//DBG_871X("+rtw_reordering_ctrl_timeout_handler()=>\n");

	spin_lock_bh(&ppending_recvframe_queue->lock);

	if(recv_indicatepkts_in_order(rtlpriv, preorder_ctrl, true)==true)
	{
		_set_timer(&preorder_ctrl->reordering_ctrl_timer, REORDER_WAIT_TIME);
	}

	spin_unlock_bh(&ppending_recvframe_queue->lock);

}

int process_recv_indicatepkts(struct rtl_priv *rtlpriv, struct recv_frame *prframe);
int process_recv_indicatepkts(struct rtl_priv *rtlpriv, struct recv_frame *prframe)
{
	int retval = _SUCCESS;
	//struct recv_priv *precvpriv = &rtlpriv->recvpriv;
	//struct rx_pkt_attrib *pattrib = &prframe->u.hdr.attrib;
	struct mlme_priv	*pmlmepriv = &rtlpriv->mlmepriv;

	struct ht_priv	*phtpriv = &pmlmepriv->htpriv;

	if(phtpriv->ht_option==true)  //B/G/N Mode
	{
		//prframe->u.hdr.preorder_ctrl = &precvpriv->recvreorder_ctrl[pattrib->priority];

		if(recv_indicatepkt_reorder(rtlpriv, prframe)!=_SUCCESS)// including perform A-MPDU Rx Ordering Buffer Control
		{

			if ((rtlpriv->bDriverStopped == false) &&
			    (rtlpriv->bSurpriseRemoved == false))
			{
				retval = _FAIL;
				return retval;
			}
		}
	}
	else //B/G mode
	{
		retval=wlanhdr_to_ethhdr (prframe);
		if(retval != _SUCCESS)
		{
			return retval;
		}

		if ((rtlpriv->bDriverStopped ==false)&&( rtlpriv->bSurpriseRemoved==false))
		{
			//indicate this recv_frame
			rtw_recv_indicatepkt(rtlpriv, prframe);


		}
		else
		{
			retval = _FAIL;
			return retval;
		}

	}

	return retval;

}

int recv_func_prehandle(struct rtl_priv *rtlpriv, struct recv_frame *rframe)
{
	int ret = _SUCCESS;
	struct rx_pkt_attrib *pattrib = &rframe->attrib;
	struct recv_priv *precvpriv = &rtlpriv->recvpriv;
	struct __queue *pfree_recv_queue = &rtlpriv->recvpriv.free_recv_queue;

	//check the frame crtl field and decache
	ret = validate_recv_frame(rtlpriv, rframe);
	if (ret != _SUCCESS)
	{
		rtw_free_recvframe(rframe, pfree_recv_queue);//free this recv_frame
		goto exit;
	}

exit:
	return ret;
}

int recv_func_posthandle(struct rtl_priv *rtlpriv, struct recv_frame *prframe)
{
	int ret = _SUCCESS;
	struct recv_frame *orig_prframe = prframe;
	struct rx_pkt_attrib *pattrib = &prframe->attrib;
	struct recv_priv *precvpriv = &rtlpriv->recvpriv;
	struct __queue *pfree_recv_queue = &rtlpriv->recvpriv.free_recv_queue;




	// DATA FRAME
	prframe = decryptor(rtlpriv, prframe);
	if (prframe == NULL) {
		ret = _FAIL;
		goto _recv_data_drop;
	}

	prframe = recvframe_chk_defrag(rtlpriv, prframe);
	if(prframe==NULL)	{
		goto _recv_data_drop;
	}

	prframe=portctrl(rtlpriv, prframe);
	if (prframe == NULL) {
		ret = _FAIL;
		goto _recv_data_drop;
	}

	count_rx_stats(rtlpriv, prframe, NULL);

	ret = process_recv_indicatepkts(rtlpriv, prframe);
	if (ret != _SUCCESS)
	{
		rtw_free_recvframe(orig_prframe, pfree_recv_queue);//free this recv_frame
		goto _recv_data_drop;
	}

_exit_recv_func:
	return ret;

_recv_data_drop:
	precvpriv->rx_drop++;
	return ret;
}


int recv_func(struct rtl_priv *rtlpriv, struct recv_frame *rframe);
int recv_func(struct rtl_priv *rtlpriv, struct recv_frame *rframe)
{
	int ret;
	struct rx_pkt_attrib *prxattrib = &rframe->attrib;
	struct recv_priv *recvpriv = &rtlpriv->recvpriv;
	struct security_priv *psecuritypriv=&rtlpriv->securitypriv;
	struct mlme_priv *mlmepriv = &rtlpriv->mlmepriv;

	/* check if need to handle uc_swdec_pending_queue*/
	if (check_fwstate(mlmepriv, WIFI_STATION_STATE) && psecuritypriv->busetkipkey)
	{
		struct recv_frame *pending_frame;

		while((pending_frame=rtw_alloc_recvframe(&rtlpriv->recvpriv.uc_swdec_pending_queue))) {
			if (recv_func_posthandle(rtlpriv, pending_frame) == _SUCCESS)
				DBG_871X("%s: dequeue uc_swdec_pending_queue\n", __func__);
		}
	}

	ret = recv_func_prehandle(rtlpriv, rframe);

	if(ret == _SUCCESS) {

		/* check if need to enqueue into uc_swdec_pending_queue*/
		if (check_fwstate(mlmepriv, WIFI_STATION_STATE) &&
			!is_multicast_ether_addr(prxattrib->ra) && prxattrib->encrypt>0 &&
			(prxattrib->bdecrypted == 0 ||psecuritypriv->sw_decrypt == true) &&
			!is_wep_enc(psecuritypriv->dot11PrivacyAlgrthm) &&
			!psecuritypriv->busetkipkey) {
			rtw_enqueue_recvframe(rframe, &rtlpriv->recvpriv.uc_swdec_pending_queue);
			DBG_871X("%s: no key, enqueue uc_swdec_pending_queue\n", __func__);
			goto exit;
		}

		ret = recv_func_posthandle(rtlpriv, rframe);
	}

exit:
	return ret;
}


int32_t rtw_recv_entry(struct recv_frame *precvframe)
{
	struct rtl_priv *rtlpriv;
	struct recv_priv *precvpriv;
	int32_t ret=_SUCCESS;



//	RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("+rtw_recv_entry\n"));

	rtlpriv = precvframe->rtlpriv;

	precvpriv = &rtlpriv->recvpriv;


	if ((ret = recv_func(rtlpriv, precvframe)) == _FAIL)
	{
		goto _recv_entry_drop;
	}


	precvpriv->rx_pkts++;



	return ret;

_recv_entry_drop:

	//RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("_recv_entry_drop\n"));



	return ret;
}

#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
void rtw_signal_stat_timer_hdl(RTW_TIMER_HDL_ARGS){
	struct rtl_priv *rtlpriv = (struct rtl_priv *)FunctionContext;
	struct recv_priv *recvpriv = &rtlpriv->recvpriv;

	uint32_t	 tmp_s, tmp_q;
	uint8_t avg_signal_strength = 0;
	uint8_t avg_signal_qual = 0;
	uint32_t	 num_signal_strength = 0;
	uint32_t	 num_signal_qual = 0;
	uint8_t _alpha = 3; // this value is based on converging_constant = 5000 and sampling_interval = 1000

	if(rtlpriv->recvpriv.is_signal_dbg) {
		//update the user specific value, signal_strength_dbg, to signal_strength, rssi
		rtlpriv->recvpriv.signal_strength= rtlpriv->recvpriv.signal_strength_dbg;
		rtlpriv->recvpriv.rssi=(s8)translate_percentage_to_dbm((uint8_t)rtlpriv->recvpriv.signal_strength_dbg);
	} else {

		if(recvpriv->signal_strength_data.update_req == 0) {// update_req is clear, means we got rx
			avg_signal_strength = recvpriv->signal_strength_data.avg_val;
			num_signal_strength = recvpriv->signal_strength_data.total_num;
			// after avg_vals are accquired, we can re-stat the signal values
			recvpriv->signal_strength_data.update_req = 1;
		}

		if(recvpriv->signal_qual_data.update_req == 0) {// update_req is clear, means we got rx
			avg_signal_qual = recvpriv->signal_qual_data.avg_val;
			num_signal_qual = recvpriv->signal_qual_data.total_num;
			// after avg_vals are accquired, we can re-stat the signal values
			recvpriv->signal_qual_data.update_req = 1;
		}

		//update value of signal_strength, rssi, signal_qual
		if(check_fwstate(&rtlpriv->mlmepriv, _FW_UNDER_SURVEY) == false) {
			tmp_s = (avg_signal_strength+(_alpha-1)*recvpriv->signal_strength);
			if(tmp_s %_alpha)
				tmp_s = tmp_s/_alpha + 1;
			else
				tmp_s = tmp_s/_alpha;
			if(tmp_s>100)
				tmp_s = 100;

			tmp_q = (avg_signal_qual+(_alpha-1)*recvpriv->signal_qual);
			if(tmp_q %_alpha)
				tmp_q = tmp_q/_alpha + 1;
			else
				tmp_q = tmp_q/_alpha;
			if(tmp_q>100)
				tmp_q = 100;

			recvpriv->signal_strength = tmp_s;
			recvpriv->rssi = (s8)translate_percentage_to_dbm(tmp_s);
			recvpriv->signal_qual = tmp_q;

		}
	}
	rtw_set_signal_stat_timer(recvpriv);

}
#endif //CONFIG_NEW_SIGNAL_STAT_PROCESS



