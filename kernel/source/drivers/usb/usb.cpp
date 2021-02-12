#include <Luna/drivers/usb/usb.hpp>
#include <Luna/misc/log.hpp>

#include <Luna/drivers/usb/xhci/xhci.hpp>

static void set_configuration(usb::Device& dev, uint8_t n) {
    auto v = dev.hci.ep0_control_xfer(dev.hci.userptr, {.packet = {.type = usb::request_type::host_to_device | usb::request_type::to_standard | usb::request_type::device, 
                                                  .request = usb::request_ops::set_configuration,
                                                  .value = n},
                                        .write = false,
                                        .len = 0});

    ASSERT(v);
}

/*static void set_interface(usb::Device& dev, uint8_t n) {
    auto v = dev.hci.ep0_control_xfer(dev.hci.userptr, {.packet = {.type = usb::request_type::host_to_device | usb::request_type::to_standard | usb::request_type::device, 
                                                  .request = usb::request_ops::set_interface,
                                                  .value = n},
                                        .write = false,
                                        .len = 0});

    ASSERT(v);
}*/

static void get_descriptor(usb::Device& dev, uint16_t len, uint8_t* buf, uint8_t type, uint8_t index = 0, uint16_t language_id = 0) {
    auto v = dev.hci.ep0_control_xfer(dev.hci.userptr, {.packet = {.type = usb::request_type::device_to_host | usb::request_type::to_standard | usb::request_type::device, 
                                                  .request = usb::request_ops::get_descriptor,
                                                  .value = (uint16_t)((type << 8) | index),
                                                  .index = language_id,
                                                  .length = len},
                                        .write = false,
                                        .len = len,
                                        .buf = buf});

    ASSERT(v);
}

static void get_configuration(usb::Device& dev, uint8_t i) {
    usb::ConfigDescriptor desc{};
    get_descriptor(dev, sizeof(usb::ConfigDescriptor), (uint8_t*)&desc, usb::descriptor_types::config, i);


    auto* buf = new uint8_t[desc.total_length];

    get_descriptor(dev, desc.total_length, (uint8_t*)buf, usb::descriptor_types::config, i);

    auto& config = dev.configs[i];
    config.desc = *(usb::ConfigDescriptor*)buf;

    size_t off = desc.length;
    for(size_t i = 0; i < config.desc.n_interfaces; i++) {
        auto& interface = *(usb::InterfaceDescriptor*)(buf + off);
        ASSERT(interface.type == usb::descriptor_types::interface);
        auto& to = config.interfaces.emplace_back();

        to.desc = interface;
        off += interface.length;

        for(size_t j = 0; j < interface.n_endpoints; j++) {
            auto& ep = *(usb::EndpointDescriptor*)(buf + off);
            ASSERT(ep.type == usb::descriptor_types::endpoint);
            auto& to_ep = to.eps.emplace_back();
            to_ep.desc = ep;

            off += ep.length;
            auto& companion = *(usb::EndpointCompanion*)(buf + off);
            if(companion.type == usb::descriptor_types::ep_companion) {
                to_ep.companion = companion;
                off += companion.length;
            }
        }
    }

    delete[] buf;
}

static void print_string(usb::Device& dev, uint8_t i, const char* prefix, uint32_t alternative) {
    if(i == 0) {
        print("{}{}\n", prefix, alternative);
    } else {
        usb::StringUnicodeDescriptor str{};
        get_descriptor(dev, 2, (uint8_t*)&str, usb::descriptor_types::string, i, dev.langid); // First the the size
        get_descriptor(dev, str.length, (uint8_t*)&str, usb::descriptor_types::string, i, dev.langid); // Now everything

        print("{}", prefix);
        for(int i = 0; i < (str.length - 2) / 2; i++) {
            auto c = (char)str.str[i];
            print("{}", c);
        }
        print("\n");
    }
}

static std::vector<usb::Device> devices;

void usb::register_device(usb::DeviceDriver& driver) {
    auto& dev = devices.emplace_back();
    dev.hci = driver;

    if(!dev.hci.addressed)
        PANIC("TODO: Send ADDRESS_DEVICE packet");
    
    get_descriptor(dev, sizeof(DeviceDescriptor), (uint8_t*)&dev.device_descriptor, descriptor_types::device);
    
    dev.configs.resize(dev.device_descriptor.num_configs);
    get_configuration(dev, 0);
}

extern "C" uintptr_t _usb_drivers_start;
extern "C" uintptr_t _usb_drivers_end;

void usb::init() {
    xhci::init();

    auto* start = (Driver**)&_usb_drivers_start;
    auto* end = (Driver**)&_usb_drivers_end;
    size_t size = end - start;
    auto find = [&](usb::Device& dev, uint8_t class_code, uint8_t subclass_code, uint8_t protocol_code) -> Driver* {
        for(size_t i = 0; i < size; i++) {
            auto& driver = *start[i];

            if(driver.match == 0)
                continue;

            if(driver.match & match::version && driver.version != dev.device_descriptor.usb_version)
                continue;

            if(driver.match & match::class_code && driver.class_code != class_code)
                continue;

            if(driver.match & match::subclass_code && driver.subclass_code != subclass_code)
                continue;

            if(driver.match & match::protocol_code && driver.protocol_code != protocol_code)
                continue;

            if(driver.match & match::vendor_product) {
                bool found = false;
                for(const auto [vid, pid] : driver.id_list) {
                    if(dev.device_descriptor.vendor_id == vid && dev.device_descriptor.product_id == pid) {
                        found = true;
                        break; // We don't have to touch suitable since suitable = suitable && true is a no-op
                    }
                }
                
                if(!found)
                    continue;
            }

            
            return &driver;
        }

        return nullptr;
    };

    for(auto& dev : devices) {
        usb::StringLanguageDescriptor lang{};
        get_descriptor(dev, 2, (uint8_t*)&lang, descriptor_types::string, 0); // First the the size
        get_descriptor(dev, lang.length, (uint8_t*)&lang, descriptor_types::string, 0); // Now everything
        dev.langid = lang.lang_ids[0]; // Just pick the first one for now

        print("usb: Registered USB {:x}.{:x} Device\n", dev.device_descriptor.usb_version >> 8, dev.device_descriptor.usb_version & 0xFF);

        auto vid = dev.device_descriptor.vendor_id, pid = dev.device_descriptor.product_id;
        print_string(dev, dev.device_descriptor.manufacturer_str, "     Vendor: ", vid);
        print_string(dev, dev.device_descriptor.product_str, "     Product: ", pid);

        for(size_t i = 0; i < dev.configs.size(); i++) {
            print_string(dev, dev.configs[i].desc.config_str, "     Config: ", dev.configs[i].desc.config_val);

            for(size_t j = 0; j < dev.configs[i].interfaces.size(); j++) {
                auto& interface = dev.configs[i].interfaces[j];
                print_string(dev, interface.desc.interface_str, "             Interface: ", interface.desc.num);
                print("             ID: {:x}.{:x}.{:x}\n", interface.desc.class_code, interface.desc.subclass_code, interface.desc.protocol);


                auto* driver = find(dev, interface.desc.class_code, interface.desc.subclass_code, interface.desc.protocol);
                if(driver) {
                    dev.curr_config = i;
                    dev.curr_interface = j;
                    dev.driver = driver;

                    print("     Driver: {}\n", driver->name);

                    set_configuration(dev, dev.configs[i].desc.config_val);
                    
                    ASSERT(dev.driver->init);
                    dev.driver->init(dev);

                    goto found;
                }
            }
        }

        found:
        ;
    }
}

void usb::Device::configure() {
    set_configuration(*this, configs[curr_config].desc.config_val);
}

uint8_t usb::Device::find_ep(bool in, uint8_t type) {
    auto& interface = configs[curr_config].interfaces[curr_interface];

    for(auto& ep : interface.eps)
        if(ep.desc.dir == in && ep.desc.ep_type == type)
            return ep.desc.ep_num;

    return -1;
}

usb::Endpoint& usb::Device::setup_ep(uint8_t ep_num) {
    auto& interface = configs[curr_config].interfaces[curr_interface];

    for(auto& ep : interface.eps) {
        if(ep.desc.ep_num == ep_num) {
            ASSERT(hci.setup_ep(hci.userptr, ep));

            auto& ctx = endpoints.emplace_back();
            ctx.data = ep;
            ctx.device = this;
            
            return ctx;
        }
    }

    PANIC("Unable to setup EP");
}

void usb::Endpoint::xfer(std::span<uint8_t> xfer) {
    ASSERT(device->hci.ep_bulk_xfer(device->hci.userptr, (2 * data.desc.ep_num) + data.desc.dir, xfer));
}