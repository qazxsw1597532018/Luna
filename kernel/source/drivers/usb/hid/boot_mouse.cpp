#include <Luna/common.hpp>
#include <Luna/drivers/usb/usb.hpp>
#include <Luna/misc/log.hpp>

#include <Luna/gui/gui.hpp>

struct [[gnu::packed]] BootReport {
    uint8_t buttons;
    int8_t x;
    int8_t y;
};

constexpr uint8_t set_idle_cmd = 0xA;

struct Device {
    usb::Device* usb_dev;
    usb::Endpoint* in;

    EventQueue<gui::GuiEvent>* queue;

    void set_idle();
};


void Device::set_idle() {
    usb_dev->hci.ep0_control_xfer(usb_dev->hci.userptr, {.packet = {.type = usb::spec::request_type::host_to_device | usb::spec::request_type::to_class | usb::spec::request_type::interface, 
                                                                    .request = set_idle_cmd,
                                                                    .value = 0,
                                                                    .index = usb_dev->curr_interface
                                                                    },
                                                                    .write = false,
                                                                    .len = 0});
}

static void init(usb::Device& device) {
    auto* dev = new Device{};
    dev->usb_dev = &device;
    dev->queue = &gui::get_desktop().get_event_queue();

    const auto in_data = dev->usb_dev->find_ep(true, usb::spec::ep_type::irq);

    dev->in = &dev->usb_dev->setup_ep(in_data);

    dev->usb_dev->configure();

    print("usb/hid_mouse: IN EP{}\n", in_data.desc.ep_num);

    dev->set_idle();

    spawn([dev] {
        while(true) {
            BootReport report{};
            std::span<uint8_t> data{(uint8_t*)&report, sizeof(report)};

            dev->in->xfer(data)->await();
            dev->queue->push(gui::GuiEvent{.type = gui::GuiEvent::Type::MouseUpdate, .pos = {report.x, report.y}});
        }
    });
}

static usb::Driver driver = {
    .name = "USB Boot Protocol Mouse Driver",
    .init = init,
    .match = usb::match::class_code | usb::match::subclass_code | usb::match::protocol_code,
    
    .class_code = 0x3, // HID
    .subclass_code = 0x1, // Boot Interface
    .protocol_code = 0x2, // Mouse
};
DECLARE_USB_DRIVER(driver);