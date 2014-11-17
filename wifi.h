#ifndef __WIFI_H__
#define __WIFI_H__

struct rtl_hal_ops;

struct rtl_priv {
	int	DriverState;// for disable driver using module, use dongle to replace module.
	int	pid[3];//process id from UI, 0:wps, 1:hostapd, 2:dhcpcd
	int	bDongle;//build-in module or external dongle
	u16 	chip_type;
	u16	HardwareType;
	u16	interface_type;//USB,SDIO,SPI,PCI

	struct dvobj_priv *dvobj;
	struct	mlme_priv mlmepriv;
	struct	mlme_ext_priv mlmeextpriv;
	struct	cmd_priv	cmdpriv;
	struct	evt_priv	evtpriv;
	//struct	io_queue	*pio_queue;
	struct 	io_priv	iopriv;
	struct	xmit_priv	xmitpriv;
	struct	recv_priv	recvpriv;
	struct	sta_priv	stapriv;
	struct	security_priv	securitypriv;
	struct	registry_priv	registrypriv;
	struct	pwrctrl_priv	pwrctrlpriv;
	struct 	eeprom_priv eeprompriv;
	struct	led_priv	ledpriv;

#ifdef CONFIG_AP_MODE
	struct	hostapd_priv	*phostapdpriv;
#endif

	u32	setband;
	PVOID			HalData;
	struct rtl_hal_ops	*HalFunc;

	int32_t	bDriverStopped;
	int32_t	bSurpriseRemoved;
	int32_t  bCardDisableWOHSM;

	u32	IsrContent;
	u32	ImrContent;

	uint8_t	EepromAddressSize;
	uint8_t	hw_init_completed;
	uint8_t	bDriverIsGoingToUnload;
	uint8_t	init_adpt_in_progress;
	uint8_t	bHaltInProgress;

	_thread_hdl_ cmdThread;
	_thread_hdl_ evtThread;
	_thread_hdl_ xmitThread;
	_thread_hdl_ recvThread;

#ifndef PLATFORM_LINUX
	NDIS_STATUS (*dvobj_init)(struct dvobj_priv *dvobj);
	void (*dvobj_deinit)(struct dvobj_priv *dvobj);
#endif

	void (*intf_start)(struct rtl_priv * adapter);
	void (*intf_stop)(struct rtl_priv * adapter);


#ifdef PLATFORM_LINUX
	struct net_device *ndev;

	int bup;
	struct net_device_stats stats;
	struct iw_statistics iwstats;
	struct proc_dir_entry *dir_dev;// for proc directory

#endif //end of PLATFORM_LINUX

	int net_closed;

	uint8_t bFWReady;
	uint8_t bBTFWReady;
	uint8_t bReadPortCancel;
	uint8_t bWritePortCancel;
	uint8_t bLinkInfoDump;
	//	Added by Albert 2012/10/26
	//	The driver will show up the desired channel number when this flag is 1.
	uint8_t bNotifyChannelChange;
#ifdef CONFIG_AUTOSUSPEND
	uint8_t	bDisableAutosuspend;
#endif
        uint8_t    fix_rate;

	unsigned char     in_cta_test;

};

struct rtl_hal_ops {
	/*
	 * New HAL functions with struct net_device  as first param
	 * this can be (hopefully)switched to struct ieee80211_hw
	 */
	int (*init_sw_vars) (struct net_device *ndev);
	void (*deinit_sw_vars) (struct net_device *ndev);

	void	(*init_sw_leds)(struct rtl_priv *padapter);

	u32	(*get_bbreg)(struct rtl_priv *padapter, u32 RegAddr, u32 BitMask);
	void	(*set_bbreg)(struct rtl_priv *padapter, u32 RegAddr, u32 BitMask, u32 Data);
	u32	(*get_rfreg)(struct rtl_priv *padapter, u32 eRFPath, u32 RegAddr, u32 BitMask);
	void	(*set_rfreg)(struct rtl_priv *padapter, u32 eRFPath, u32 RegAddr, u32 BitMask, u32 Data);

	/* Old HAL functions */

	u32	(*hal_init)(struct rtl_priv *padapter);
	u32	(*hal_deinit)(struct rtl_priv *padapter);

	void	(*free_hal_data)(struct rtl_priv *padapter);

	u32	(*inirp_init)(struct rtl_priv *padapter);
	u32	(*inirp_deinit)(struct rtl_priv *padapter);

	int32_t	(*init_xmit_priv)(struct rtl_priv *padapter);
	void	(*free_xmit_priv)(struct rtl_priv *padapter);

	int32_t	(*init_recv_priv)(struct rtl_priv *padapter);
	void	(*free_recv_priv)(struct rtl_priv *padapter);

	void	(*DeInitSwLeds)(struct rtl_priv *padapter);

	void	(*dm_init)(struct rtl_priv *padapter);
	void	(*dm_deinit)(struct rtl_priv *padapter);
	void	(*read_chip_version)(struct rtl_priv *padapter);

	void	(*init_default_value)(struct rtl_priv *padapter);

	void	(*intf_chip_configure)(struct rtl_priv *padapter);

	void	(*read_adapter_info)(struct rtl_priv *padapter);

	void	(*enable_interrupt)(struct rtl_priv *padapter);
	void	(*disable_interrupt)(struct rtl_priv *padapter);
	int32_t	(*interrupt_handler)(struct rtl_priv *padapter);

	void	(*set_bwmode_handler)(struct rtl_priv *padapter, CHANNEL_WIDTH Bandwidth, uint8_t Offset);
	void	(*set_channel_handler)(struct rtl_priv *padapter, uint8_t channel);
	void	(*set_chnl_bw_handler)(struct rtl_priv *padapter, uint8_t channel, CHANNEL_WIDTH Bandwidth, uint8_t Offset40, uint8_t Offset80);

	void	(*hal_dm_watchdog)(struct rtl_priv *padapter);

	void	(*SetHwRegHandler)(struct rtl_priv *padapter, uint8_t	variable,u8* val);
	void	(*GetHwRegHandler)(struct rtl_priv *padapter, uint8_t	variable,u8* val);

	uint8_t	(*GetHalDefVarHandler)(struct rtl_priv *padapter, HAL_DEF_VARIABLE eVariable, PVOID pValue);
	uint8_t	(*SetHalDefVarHandler)(struct rtl_priv *padapter, HAL_DEF_VARIABLE eVariable, PVOID pValue);

	void	(*GetHalODMVarHandler)(struct rtl_priv *padapter, HAL_ODM_VARIABLE eVariable, PVOID pValue1,BOOLEAN bSet);
	void	(*SetHalODMVarHandler)(struct rtl_priv *padapter, HAL_ODM_VARIABLE eVariable, PVOID pValue1,BOOLEAN bSet);

	void	(*UpdateRAMaskHandler)(struct rtl_priv *padapter, u32 mac_id, uint8_t rssi_level);
	void	(*SetBeaconRelatedRegistersHandler)(struct rtl_priv *padapter);

	void	(*Add_RateATid)(struct rtl_priv *padapter, u32 bitmap, u8* arg, uint8_t rssi_level);
#ifdef CONFIG_ANTENNA_DIVERSITY
	uint8_t	(*AntDivBeforeLinkHandler)(struct rtl_priv *padapter);
	void	(*AntDivCompareHandler)(struct rtl_priv *padapter, WLAN_BSSID_EX *dst, WLAN_BSSID_EX *src);
#endif
	uint8_t	(*interface_ps_func)(struct rtl_priv *padapter,HAL_INTF_PS_FUNC efunc_id, u8* val);

	int32_t	(*hal_xmit)(struct rtl_priv *padapter, struct xmit_frame *pxmitframe);
	int32_t (*mgnt_xmit)(struct rtl_priv *padapter, struct xmit_frame *pmgntframe);
	int32_t	(*hal_xmitframe_enqueue)(struct rtl_priv *padapter, struct xmit_frame *pxmitframe);

	void (*EfusePowerSwitch)(struct rtl_priv *padapter, uint8_t bWrite, uint8_t PwrState);
	void (*ReadEFuse)(struct rtl_priv *padapter, uint8_t efuseType, u16 _offset, u16 _size_byte, uint8_t *pbuf);
	void (*EFUSEGetEfuseDefinition)(struct rtl_priv *padapter, uint8_t efuseType, uint8_t type, void *pOut);
	u16	(*EfuseGetCurrentSize)(struct rtl_priv *padapter, uint8_t efuseType);
	int 	(*Efuse_PgPacketRead)(struct rtl_priv *padapter, uint8_t offset, uint8_t *data);
	int 	(*Efuse_PgPacketWrite)(struct rtl_priv *padapter, uint8_t offset, uint8_t word_en, uint8_t *data);
	uint8_t	(*Efuse_WordEnableDataWrite)(struct rtl_priv *padapter, u16 efuse_addr, uint8_t word_en, uint8_t *data);
	BOOLEAN	(*Efuse_PgPacketWrite_BT)(struct rtl_priv *padapter, uint8_t offset, uint8_t word_en, uint8_t *data);

#ifdef DBG_CONFIG_ERROR_DETECT
	void (*sreset_init_value)(struct rtl_priv *padapter);
	void (*sreset_reset_value)(struct rtl_priv *padapter);
	void (*silentreset)(struct rtl_priv *padapter);
	void (*sreset_xmit_status_check)(struct rtl_priv *padapter);
	void (*sreset_linked_status_check) (struct rtl_priv *padapter);
	uint8_t (*sreset_get_wifi_status)(struct rtl_priv *padapter);
	bool (*sreset_inprogress)(struct rtl_priv *padapter);
#endif
	void (*hal_notch_filter)(struct rtl_priv * adapter, bool enable);
	void (*hal_reset_security_engine)(struct rtl_priv * adapter);
	int32_t (*c2h_handler)(struct rtl_priv *padapter, struct c2h_evt_hdr *c2h_evt);
	c2h_id_filter c2h_id_filter_ccx;
};


#endif