#include <Luna/common.hpp>
#include <Luna/drivers/usb/usb.hpp>
#include <Luna/misc/log.hpp>

#include <Luna/drivers/storage/scsi.hpp>

constexpr uint8_t req_bulk_only_reset = 0xFF;
constexpr uint8_t req_bulk_only_get_lun = 0xFE;

constexpr uint32_t cbw_sig = 0x43425355;
constexpr uint32_t csw_sig = 0x53425355;

constexpr uint8_t cbw_dir_out = 0;
constexpr uint8_t cbw_dir_in = (1 << 7);

struct [[gnu::packed]] CBW {
    CBW(): sig{cbw_sig} {}

    uint32_t sig;
    uint32_t tag;
    uint32_t len;
    uint8_t flags;
    uint8_t lun;
    uint8_t cmd_len;
    uint8_t scsi_cmd[16];

    std::span<uint8_t> span() {
        return {(uint8_t*)this, sizeof(CBW)};
    }
};

struct [[gnu::packed]] CSW {
    uint32_t sig;
    uint32_t tag;
    uint32_t residue;
    uint8_t status;

    std::span<uint8_t> span() {
        return {(uint8_t*)this, sizeof(CSW)};
    }
};



struct Device {
    usb::Device* usb_dev;

    usb::Endpoint* out, *in;

    uint32_t tag;
};



void init(usb::Device& device) {
    auto* dev = new Device{};
    dev->usb_dev = &device;
    dev->tag = 1;

    auto out_n = dev->usb_dev->find_ep(false, usb::ep_type::bulk);
    auto in_n = dev->usb_dev->find_ep(true, usb::ep_type::bulk);

    dev->in = &dev->usb_dev->setup_ep(in_n);
    dev->out = &dev->usb_dev->setup_ep(out_n);

    dev->usb_dev->configure();

    print("usb/msd: OUT: EP{}, IN: EP{}\n", out_n, in_n);

    /*ASSERT(dev->usb_dev->hci.ep0_control_xfer(dev->usb_dev->hci.userptr, {.packet = {.type = usb::request_type::host_to_device | usb::request_type::to_class | usb::request_type::interface, 
                                                                              .request = req_bulk_only_reset,
                                                                              .index = dev->usb_dev->curr_interface},
                                                                    .write = false,
                                                                    .len = 0}));

    uint8_t max_lun = 0;
    ASSERT(dev->usb_dev->hci.ep0_control_xfer(dev->usb_dev->hci.userptr, {.packet = {.type = usb::request_type::device_to_host | usb::request_type::to_class | usb::request_type::interface, 
                                                                              .request = 0xFE,
                                                                              .index = dev->usb_dev->curr_interface,
                                                                              .length = 1},
                                                                    .write = false,
                                                                    .len = 1,
                                                                    .buf = &max_lun}));*/

    scsi::DriverDevice scsi_dev{};
    scsi_dev.max_packet_size = 16;
    scsi_dev.userptr = dev;
    scsi_dev.scsi_cmd = [](void* userptr, const scsi::SCSICommand& cmd, std::span<uint8_t>& xfer) {
        auto& device = *(Device*)userptr;

        CBW cbw{};
        cbw.tag = device.tag++;

        cbw.len = xfer.size_bytes();
        cbw.flags = cmd.write ? cbw_dir_out : cbw_dir_in;
        cbw.lun = 0;
        cbw.cmd_len = cmd.packet_len;
        memset(cbw.scsi_cmd, 0, 16);
        memcpy(cbw.scsi_cmd, cmd.packet, cmd.packet_len);
        device.out->xfer(cbw.span());

        if(cmd.write)
            device.out->xfer(xfer);
        else
            device.in->xfer(xfer);

        CSW csw{};
        device.in->xfer(csw.span());

        ASSERT(csw.sig == csw_sig);

        if(auto status = csw.status; status != 0)
            print("usb/msd: Failure: Status: {}\n", status);

        if(auto residue = csw.residue; residue != 0)
            print("usb/msd: Residue: {:#x}\n", residue);
    };

    scsi::register_device(scsi_dev);
}

static usb::Driver driver = {
    .name = "USB Mass Storage Driver",
    .init = init,
    .version = {
        .bind = false
    },

    .proto = {
        .bind = true,
        .class_code = 0x8, // Mass Storage
        .subclass_code = 0x6, // SCSI
        .prog_if = 0x50 // Bulk Only
    }
};
DECLARE_USB_DRIVER(driver);