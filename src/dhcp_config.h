#include <zephyr.h>
#include <net/net_if.h>
#include <net/net_core.h>
#include <net/net_context.h>
#include <net/net_mgmt.h>
#include <device.h>

static struct net_mgmt_event_callback mgmt_cb;

static void dhcp_handler(struct net_mgmt_event_callback *cb, 
							uint32_t mgmt_event, 
							struct net_if *iface){
	int i = 0;
	if (mgmt_event != NET_EVENT_IPV4_ADDR_ADD) {
		printk("not listening to right event\n");
		return;
	}

	for (i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
		char buf[NET_IPV4_ADDR_LEN];

		if (iface->config.ip.ipv4->unicast[i].addr_type != NET_ADDR_DHCP) {
			continue;
		}

		printk("Assigned address: %s\n", 
				log_strdup(
					net_addr_ntop(AF_INET, &iface->config.ip.ipv4->unicast[i].address.in_addr, buf, sizeof(buf))));
		
		printk("Lease time: %u hours\n", iface->config.dhcpv4.lease_time / 60 / 60);
		printk("Subnet: %s\n", 
				log_strdup(
					net_addr_ntop(AF_INET, &iface->config.ip.ipv4->netmask, buf, sizeof(buf))));
		printk("Gateway: %s\n", 
				log_strdup(
					net_addr_ntop(AF_INET, &iface->config.ip.ipv4->gw, buf, sizeof(buf))));
	}
}

void initialize_dhcp() {
    struct net_if *iface;
	net_mgmt_init_event_callback(&mgmt_cb, dhcp_handler, NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&mgmt_cb);
	iface = net_if_get_default();
	net_dhcpv4_start(iface);
}
