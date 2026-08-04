// BambuLAN - a small iPad app for exercising the native Bambu LAN backend
// ----------------------------------------------------------------------
// Builds in seconds and links exactly the code Orca will use
// (orca-overlay/src/slic3r/Utils/BambuLan*.cpp), so the protocol work can be
// tested against a real printer without waiting for the full OrcaSlicer build.
//
// Everything it sends is the same JSON Orca's MachineObject sends: the command
// payloads here were copied from src/slic3r/GUI/DeviceManager.cpp and
// DeviceCore/Dev*.cpp so that a green result here means green in the app.
//
// Copyright (C) 2026 Orca-iOS-ipa contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <objc/runtime.h>

#include <atomic>
#include <string>

#include "BambuLanDiscovery.hpp"
#include "BambuLanMqtt.hpp"
#include "BambuLanPrintCommand.hpp"
#ifdef BAMBU_LAN_WITH_FTPS
#include "BambuLanFtps.hpp"
#endif

#include <nlohmann/json.hpp>

using json = nlohmann::json;
using Slic3r::BambuLan::MqttClient;
using Slic3r::BambuLan::MqttConfig;
using Slic3r::BambuLan::SsdpListener;

static NSString* const kDefaultsIp     = @"printer_ip";
static NSString* const kDefaultsCode   = @"access_code";
static NSString* const kDefaultsSerial = @"serial";

// ---------------------------------------------------------------------------

@interface ViewController : UIViewController <UIDocumentPickerDelegate>
@end

@implementation ViewController {
    UITextField* _ipField;
    UITextField* _codeField;
    UITextField* _serialField;
    UITextField* _nozzleField;
    UITextField* _bedField;
    UITextField* _gcodeField;
    UILabel*     _statusLabel;
    UILabel*     _telemetryLabel;
    UITextView*  _logView;
    UIButton*    _connectButton;

    MqttClient*   _mqtt;
    SsdpListener* _ssdp;
    std::atomic<int>* _sequence;
    BOOL _connected;
    BOOL _pendingPrint;
}

// -- logging ---------------------------------------------------------------

- (void)log:(NSString*)line
{
    dispatch_async(dispatch_get_main_queue(), ^{
        NSDateFormatter* fmt = [[NSDateFormatter alloc] init];
        fmt.dateFormat = @"HH:mm:ss";
        NSString* stamp = [fmt stringFromDate:[NSDate date]];
        self->_logView.text = [NSString stringWithFormat:@"%@[%@] %@\n", self->_logView.text, stamp, line];
        const NSUInteger len = self->_logView.text.length;
        if (len > 40000)
            self->_logView.text = [self->_logView.text substringFromIndex:len - 30000];
        [self->_logView scrollRangeToVisible:NSMakeRange(self->_logView.text.length, 0)];
    });
}

- (void)setStatus:(NSString*)text ok:(BOOL)ok
{
    dispatch_async(dispatch_get_main_queue(), ^{
        self->_statusLabel.text      = text;
        self->_statusLabel.textColor = ok ? [UIColor systemGreenColor] : [UIColor systemRedColor];
    });
}

// -- UI construction -------------------------------------------------------

- (UIButton*)buttonTitled:(NSString*)title action:(SEL)action
{
    UIButton* b = [UIButton buttonWithType:UIButtonTypeSystem];
    [b setTitle:title forState:UIControlStateNormal];
    b.titleLabel.font          = [UIFont systemFontOfSize:15 weight:UIFontWeightMedium];
    b.backgroundColor          = [UIColor secondarySystemBackgroundColor];
    b.layer.cornerRadius       = 8;
    b.contentEdgeInsets        = UIEdgeInsetsMake(8, 12, 8, 12);
    [b addTarget:self action:action forControlEvents:UIControlEventTouchUpInside];
    return b;
}

- (UITextField*)fieldWithPlaceholder:(NSString*)placeholder text:(NSString*)text width:(CGFloat)width
{
    UITextField* f            = [[UITextField alloc] init];
    f.placeholder             = placeholder;
    f.text                    = text;
    f.borderStyle             = UITextBorderStyleRoundedRect;
    f.autocorrectionType      = UITextAutocorrectionTypeNo;
    f.autocapitalizationType  = UITextAutocapitalizationTypeNone;
    f.font                    = [UIFont monospacedSystemFontOfSize:15 weight:UIFontWeightRegular];
    [f.widthAnchor constraintEqualToConstant:width].active = YES;
    return f;
}

- (UIStackView*)row:(NSArray*)views
{
    UIStackView* s = [[UIStackView alloc] initWithArrangedSubviews:views];
    s.axis         = UILayoutConstraintAxisHorizontal;
    s.spacing      = 8;
    s.alignment    = UIStackViewAlignmentCenter;
    return s;
}

- (UILabel*)heading:(NSString*)text
{
    UILabel* l = [[UILabel alloc] init];
    l.text     = text;
    l.font     = [UIFont systemFontOfSize:13 weight:UIFontWeightSemibold];
    l.textColor = [UIColor secondaryLabelColor];
    return l;
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    self.view.backgroundColor = [UIColor systemBackgroundColor];

    _mqtt     = new MqttClient();
    _ssdp     = new SsdpListener();
    _sequence = new std::atomic<int>(1);

    NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];

    UIScrollView* scroll  = [[UIScrollView alloc] init];
    scroll.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:scroll];

    UIStackView* stack = [[UIStackView alloc] init];
    stack.axis         = UILayoutConstraintAxisVertical;
    stack.spacing      = 10;
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    [scroll addSubview:stack];

    // --- connection
    _ipField     = [self fieldWithPlaceholder:@"192.168.1.50" text:[defaults stringForKey:kDefaultsIp] width:170];
    _codeField   = [self fieldWithPlaceholder:@"access code" text:[defaults stringForKey:kDefaultsCode] width:140];
    _serialField = [self fieldWithPlaceholder:@"serial (USN)" text:[defaults stringForKey:kDefaultsSerial] width:200];
    _ipField.keyboardType = UIKeyboardTypeDecimalPad;

    _connectButton = [self buttonTitled:@"Connect" action:@selector(onConnect)];
    _statusLabel   = [[UILabel alloc] init];
    _statusLabel.text = @"disconnected";
    _statusLabel.font = [UIFont systemFontOfSize:15 weight:UIFontWeightSemibold];
    _statusLabel.textColor = [UIColor secondaryLabelColor];

    [stack addArrangedSubview:[self heading:@"PRINTER (LAN mode: IP + access code from the printer's screen)"]];
    [stack addArrangedSubview:[self row:@[_ipField, _codeField, _serialField]]];
    [stack addArrangedSubview:[self row:@[
        _connectButton,
        [self buttonTitled:@"Disconnect" action:@selector(onDisconnect)],
        [self buttonTitled:@"Scan (SSDP)" action:@selector(onScan)],
        _statusLabel,
    ]]];

    _telemetryLabel       = [[UILabel alloc] init];
    _telemetryLabel.text  = @"no telemetry yet";
    _telemetryLabel.font  = [UIFont monospacedSystemFontOfSize:14 weight:UIFontWeightRegular];
    _telemetryLabel.numberOfLines = 2;
    [stack addArrangedSubview:_telemetryLabel];

    // --- status / info
    [stack addArrangedSubview:[self heading:@"STATUS"]];
    [stack addArrangedSubview:[self row:@[
        [self buttonTitled:@"Push all" action:@selector(onPushAll)],
        [self buttonTitled:@"Get version" action:@selector(onGetVersion)],
    ]]];

    // --- motion
    [stack addArrangedSubview:[self heading:@"MOTION"]];
    [stack addArrangedSubview:[self row:@[
        [self buttonTitled:@"Home all" action:@selector(onHome)],
        [self buttonTitled:@"X -10" action:@selector(onXMinus)],
        [self buttonTitled:@"X +10" action:@selector(onXPlus)],
        [self buttonTitled:@"Y -10" action:@selector(onYMinus)],
        [self buttonTitled:@"Y +10" action:@selector(onYPlus)],
        [self buttonTitled:@"Z -10" action:@selector(onZMinus)],
        [self buttonTitled:@"Z +10" action:@selector(onZPlus)],
    ]]];

    // --- extrusion
    [stack addArrangedSubview:[self heading:@"EXTRUDER (needs a hot nozzle - set 220 first)"]];
    [stack addArrangedSubview:[self row:@[
        [self buttonTitled:@"Extrude 10mm" action:@selector(onExtrude)],
        [self buttonTitled:@"Retract 10mm" action:@selector(onRetract)],
        [self buttonTitled:@"Unload filament" action:@selector(onUnload)],
    ]]];

    // --- temperatures
    _nozzleField = [self fieldWithPlaceholder:@"nozzle" text:@"220" width:80];
    _bedField    = [self fieldWithPlaceholder:@"bed" text:@"60" width:80];
    _nozzleField.keyboardType = UIKeyboardTypeNumberPad;
    _bedField.keyboardType    = UIKeyboardTypeNumberPad;
    [stack addArrangedSubview:[self heading:@"TEMPERATURE"]];
    [stack addArrangedSubview:[self row:@[
        _nozzleField, [self buttonTitled:@"Set nozzle" action:@selector(onSetNozzle)],
        _bedField, [self buttonTitled:@"Set bed" action:@selector(onSetBed)],
        [self buttonTitled:@"All off" action:@selector(onTempsOff)],
    ]]];

    // --- fans, light, speed
    [stack addArrangedSubview:[self heading:@"FANS / LIGHT / SPEED"]];
    [stack addArrangedSubview:[self row:@[
        [self buttonTitled:@"Part fan 0" action:@selector(onFanOff)],
        [self buttonTitled:@"Part fan 50%" action:@selector(onFanHalf)],
        [self buttonTitled:@"Part fan 100%" action:@selector(onFanFull)],
        [self buttonTitled:@"Aux fan 100%" action:@selector(onAuxFan)],
        [self buttonTitled:@"Light on" action:@selector(onLightOn)],
        [self buttonTitled:@"Light off" action:@selector(onLightOff)],
    ]]];
    [stack addArrangedSubview:[self row:@[
        [self buttonTitled:@"Silent" action:@selector(onSpeedSilent)],
        [self buttonTitled:@"Standard" action:@selector(onSpeedStandard)],
        [self buttonTitled:@"Sport" action:@selector(onSpeedSport)],
        [self buttonTitled:@"Ludicrous" action:@selector(onSpeedLudicrous)],
    ]]];

    // --- job control
    [stack addArrangedSubview:[self heading:@"JOB"]];
    [stack addArrangedSubview:[self row:@[
        [self buttonTitled:@"Pause" action:@selector(onPause)],
        [self buttonTitled:@"Resume" action:@selector(onResume)],
        [self buttonTitled:@"Stop" action:@selector(onStop)],
#ifdef BAMBU_LAN_WITH_FTPS
        [self buttonTitled:@"Upload + print a 3mf..." action:@selector(onSendPrint)],
        [self buttonTitled:@"Upload only..." action:@selector(onUploadOnly)],
        [self buttonTitled:@"List SD card" action:@selector(onListSdCard)],
#endif
    ]]];

    // --- raw
    _gcodeField = [self fieldWithPlaceholder:@"G28 ; raw gcode, \\n for newlines" text:@"" width:420];
    [stack addArrangedSubview:[self heading:@"RAW"]];
    [stack addArrangedSubview:[self row:@[
        _gcodeField,
        [self buttonTitled:@"Send gcode" action:@selector(onSendGcode)],
        [self buttonTitled:@"Send as JSON" action:@selector(onSendJson)],
    ]]];

    // --- log
    _logView                 = [[UITextView alloc] init];
    _logView.editable        = NO;
    _logView.font            = [UIFont monospacedSystemFontOfSize:12 weight:UIFontWeightRegular];
    _logView.backgroundColor = [UIColor secondarySystemBackgroundColor];
    _logView.layer.cornerRadius = 8;
    [_logView.heightAnchor constraintEqualToConstant:340].active = YES;
    [stack addArrangedSubview:[self heading:@"LOG"]];
    [stack addArrangedSubview:_logView];
    [stack addArrangedSubview:[self row:@[[self buttonTitled:@"Clear log" action:@selector(onClearLog)]]]];

    UILayoutGuide* guide = self.view.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [scroll.topAnchor constraintEqualToAnchor:guide.topAnchor],
        [scroll.leadingAnchor constraintEqualToAnchor:guide.leadingAnchor constant:16],
        [scroll.trailingAnchor constraintEqualToAnchor:guide.trailingAnchor constant:-16],
        [scroll.bottomAnchor constraintEqualToAnchor:guide.bottomAnchor],
        [stack.topAnchor constraintEqualToAnchor:scroll.topAnchor constant:12],
        [stack.leadingAnchor constraintEqualToAnchor:scroll.leadingAnchor],
        [stack.bottomAnchor constraintEqualToAnchor:scroll.bottomAnchor constant:-12],
        [stack.widthAnchor constraintEqualToAnchor:scroll.widthAnchor],
    ]];

    [self log:@"BambuLAN test app. Put the printer in LAN-only mode, then enter its IP,"];
    [self log:@"the 8-character access code and the serial number, and press Connect."];
#ifndef BAMBU_LAN_WITH_FTPS
    [self log:@"(built without libcurl: upload/print buttons are unavailable)"];
#endif
}

- (void)dealloc
{
    if (_mqtt != nullptr) {
        _mqtt->set_message_fn(nullptr);
        _mqtt->set_lost_fn(nullptr);
        delete _mqtt;
    }
    delete _ssdp;
    delete _sequence;
}

// -- helpers ---------------------------------------------------------------

- (std::string)serial { return std::string(_serialField.text.UTF8String ?: ""); }

- (int)nextSequence { return _sequence->fetch_add(1); }

- (void)publish:(const json&)command
{
    if (!_connected) {
        [self log:@"not connected"];
        return;
    }
    const std::string payload = command.dump();
    const std::string topic   = "device/" + [self serial] + "/request";
    const bool        ok      = _mqtt->publish(topic, payload, 1);
    [self log:[NSString stringWithFormat:@"%@ -> %s", ok ? @"sent" : @"SEND FAILED", payload.c_str()]];
}

// Every gcode-carrying command Orca sends goes through this shape.
- (void)sendGcode:(const std::string&)gcode
{
    json j;
    j["print"]["command"]     = "gcode_line";
    j["print"]["param"]       = gcode;
    j["print"]["sequence_id"] = std::to_string([self nextSequence]);
    [self publish:j];
}

- (void)sendPrintCommand:(const std::string&)command param:(const std::string&)param
{
    json j;
    j["print"]["command"]     = command;
    j["print"]["param"]       = param;
    j["print"]["sequence_id"] = std::to_string([self nextSequence]);
    [self publish:j];
}

// The printer pushes a lot; pull the few numbers worth showing on screen.
- (void)updateTelemetry:(const std::string&)payload
{
    const json j = json::parse(payload, nullptr, false);
    if (j.is_discarded() || !j.contains("print"))
        return;
    const json& p = j["print"];

    NSMutableArray<NSString*>* parts = [NSMutableArray array];
    auto number = [&](const char* key, NSString* label, const char* unit) {
        if (p.contains(key) && p[key].is_number())
            [parts addObject:[NSString stringWithFormat:@"%@ %.1f%s", label, p[key].get<double>(), unit]];
    };
    number("nozzle_temper", @"nozzle", "C");
    number("nozzle_target_temper", @"->", "C");
    number("bed_temper", @"bed", "C");
    number("bed_target_temper", @"->", "C");
    number("chamber_temper", @"chamber", "C");
    if (p.contains("cooling_fan_speed") && p["cooling_fan_speed"].is_string())
        [parts addObject:[NSString stringWithFormat:@"fan %s", p["cooling_fan_speed"].get<std::string>().c_str()]];
    if (p.contains("gcode_state") && p["gcode_state"].is_string())
        [parts addObject:[NSString stringWithFormat:@"state %s", p["gcode_state"].get<std::string>().c_str()]];
    if (p.contains("mc_percent") && p["mc_percent"].is_number())
        [parts addObject:[NSString stringWithFormat:@"%d%%", p["mc_percent"].get<int>()]];

    if (parts.count == 0)
        return;
    NSString* text = [parts componentsJoinedByString:@"   "];
    dispatch_async(dispatch_get_main_queue(), ^{ self->_telemetryLabel.text = text; });
}

// -- connection ------------------------------------------------------------

- (void)onConnect
{
    [self.view endEditing:YES];

    NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
    [defaults setObject:_ipField.text forKey:kDefaultsIp];
    [defaults setObject:_codeField.text forKey:kDefaultsCode];
    [defaults setObject:_serialField.text forKey:kDefaultsSerial];

    if (_ipField.text.length == 0 || _codeField.text.length == 0 || _serialField.text.length == 0) {
        [self log:@"need an IP, an access code and a serial number"];
        return;
    }

    _mqtt->set_message_fn([self](const std::string& topic, const std::string& payload) {
        [self updateTelemetry:payload];
        std::string shown = payload;
        if (shown.size() > 600)
            shown = shown.substr(0, 600) + " ...(" + std::to_string(payload.size()) + " bytes)";
        [self log:[NSString stringWithFormat:@"<- %s  %s", topic.c_str(), shown.c_str()]];
    });
    _mqtt->set_lost_fn([self](const std::string& reason) {
        self->_connected = NO;
        [self setStatus:@"connection lost" ok:NO];
        [self log:[NSString stringWithFormat:@"connection lost: %s", reason.c_str()]];
    });

    [self setStatus:@"connecting..." ok:YES];
    [self log:[NSString stringWithFormat:@"connecting to %@:8883 over TLS", _ipField.text]];

    NSString* ip     = _ipField.text;
    NSString* code   = _codeField.text;
    NSString* serial = _serialField.text;

    // Off the main thread: this is a TCP connect plus a TLS handshake.
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        MqttConfig cfg;
        cfg.host     = ip.UTF8String;
        cfg.port     = 8883;
        cfg.username = "bblp";
        cfg.password = code.UTF8String;

        std::string error;
        const int   rc = self->_mqtt->connect(cfg, error);
        if (rc != Slic3r::BambuLan::ConnackAccepted) {
            [self setStatus:@"connect failed" ok:NO];
            [self log:[NSString stringWithFormat:@"connect failed (code %d): %s", rc, error.c_str()]];
            if (rc == Slic3r::BambuLan::ConnackNotAuthorized)
                [self log:@"-> the printer rejected the access code"];
            return;
        }

        const std::string topic = std::string("device/") + serial.UTF8String + "/report";
        if (!self->_mqtt->subscribe(topic, 0)) {
            [self setStatus:@"subscribe failed" ok:NO];
            return;
        }
        self->_connected = YES;
        [self setStatus:@"connected" ok:YES];
        [self log:[NSString stringWithFormat:@"connected; subscribed to %s", topic.c_str()]];
        [self onPushAll];
    });
}

- (void)onDisconnect
{
    _connected = NO;
    _mqtt->disconnect();
    [self setStatus:@"disconnected" ok:NO];
    [self log:@"disconnected"];
}

- (void)onScan
{
    if (_ssdp->is_running()) {
        _ssdp->stop();
        [self log:@"SSDP listener stopped"];
        return;
    }
    _ssdp->set_printer_fn([self](const std::string& machine_json) {
        [self log:[NSString stringWithFormat:@"SSDP: %s", machine_json.c_str()]];
    });
    if (_ssdp->start(true))
        [self log:@"listening for SSDP announcements on 1990/2021 (tap again to stop)"];
    else
        [self log:@"could not bind the SSDP ports - enter the IP by hand"];
}

// -- status ----------------------------------------------------------------

- (void)onPushAll
{
    json j;
    j["pushing"]["sequence_id"] = std::to_string([self nextSequence]);
    j["pushing"]["command"]     = "pushall";
    j["pushing"]["version"]     = 1;
    j["pushing"]["push_target"] = 1;
    [self publish:j];
}

- (void)onGetVersion
{
    json j;
    j["info"]["sequence_id"] = std::to_string([self nextSequence]);
    j["info"]["command"]     = "get_version";
    [self publish:j];
}

// -- motion ----------------------------------------------------------------

- (void)onHome { [self sendGcode:"G28 \n"]; }

// Same preamble Orca's command_axis_control() uses: soft limits off for the
// jog, relative positioning, then everything restored.
- (void)moveAxis:(const char*)axis distance:(double)mm speed:(int)feedrate
{
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "M211 S \nM211 X1 Y1 Z1\nM1002 push_ref_mode\nG91 \nG1 %s%0.1f F%d\nM1002 pop_ref_mode\nM211 R\n",
             axis, mm, feedrate);
    [self sendGcode:std::string(cmd)];
}

- (void)onXMinus { [self moveAxis:"X" distance:-10 speed:3000]; }
- (void)onXPlus  { [self moveAxis:"X" distance:10 speed:3000]; }
// Y and Z are inverted on bed-slinger machines (A1/P1), matching Orca.
- (void)onYMinus { [self moveAxis:"Y" distance:10 speed:3000]; }
- (void)onYPlus  { [self moveAxis:"Y" distance:-10 speed:3000]; }
- (void)onZMinus { [self moveAxis:"Z" distance:10 speed:1200]; }
- (void)onZPlus  { [self moveAxis:"Z" distance:-10 speed:1200]; }

// -- extruder --------------------------------------------------------------

- (void)onExtrude { [self sendGcode:"M83 \nG0 E10.0 F900\n"]; }
- (void)onRetract { [self sendGcode:"M83 \nG0 E-10.0 F900\n"]; }

- (void)onUnload
{
    json j;
    j["print"]["command"]     = "ams_change_filament";
    j["print"]["target"]      = 255; // 255 = unload
    j["print"]["curr_temp"]   = 220;
    j["print"]["tar_temp"]    = 220;
    j["print"]["sequence_id"] = std::to_string([self nextSequence]);
    [self publish:j];
}

// -- temperatures ----------------------------------------------------------

- (void)onSetNozzle
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "M104 S%d\n", (int) _nozzleField.text.intValue);
    [self sendGcode:std::string(cmd)];
}

- (void)onSetBed
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "M140 S%d\n", (int) _bedField.text.intValue);
    [self sendGcode:std::string(cmd)];
}

- (void)onTempsOff { [self sendGcode:"M104 S0\nM140 S0\n"]; }

// -- fans / light / speed --------------------------------------------------

- (void)setFan:(int)part speed:(int)value
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "M106 P%d S%d \n", part, value);
    [self sendGcode:std::string(cmd)];
}

- (void)onFanOff  { [self setFan:1 speed:0]; }
- (void)onFanHalf { [self setFan:1 speed:127]; }
- (void)onFanFull { [self setFan:1 speed:255]; }
- (void)onAuxFan  { [self setFan:2 speed:255]; }

- (void)setLight:(const char*)mode
{
    json j;
    j["system"]["command"]       = "ledctrl";
    j["system"]["led_node"]      = "chamber_light";
    j["system"]["sequence_id"]   = std::to_string([self nextSequence]);
    j["system"]["led_mode"]      = mode;
    j["system"]["led_on_time"]   = 500;
    j["system"]["led_off_time"]  = 500;
    j["system"]["loop_times"]    = 0;
    j["system"]["interval_time"] = 0;
    [self publish:j];
}

- (void)onLightOn  { [self setLight:"on"]; }
- (void)onLightOff { [self setLight:"off"]; }

- (void)onSpeedSilent    { [self sendPrintCommand:"print_speed" param:"1"]; }
- (void)onSpeedStandard  { [self sendPrintCommand:"print_speed" param:"2"]; }
- (void)onSpeedSport     { [self sendPrintCommand:"print_speed" param:"3"]; }
- (void)onSpeedLudicrous { [self sendPrintCommand:"print_speed" param:"4"]; }

// -- job -------------------------------------------------------------------

- (void)onPause  { [self sendPrintCommand:"pause" param:""]; }
- (void)onResume { [self sendPrintCommand:"resume" param:""]; }
- (void)onStop   { [self sendPrintCommand:"stop" param:""]; }

// -- raw -------------------------------------------------------------------

- (void)onSendGcode
{
    [self.view endEditing:YES];
    NSString* text = [_gcodeField.text stringByReplacingOccurrencesOfString:@"\\n" withString:@"\n"];
    if (text.length == 0)
        return;
    if (![text hasSuffix:@"\n"])
        text = [text stringByAppendingString:@"\n"];
    [self sendGcode:std::string(text.UTF8String)];
}

- (void)onSendJson
{
    [self.view endEditing:YES];
    if (_gcodeField.text.length == 0)
        return;
    const json j = json::parse(std::string(_gcodeField.text.UTF8String), nullptr, false);
    if (j.is_discarded()) {
        [self log:@"not valid JSON"];
        return;
    }
    [self publish:j];
}

- (void)onClearLog { _logView.text = @""; }

// -- upload + print --------------------------------------------------------

#ifdef BAMBU_LAN_WITH_FTPS

- (void)pickFileThenPrint:(BOOL)print
{
    if (!_connected && print) {
        [self log:@"connect first - the print command goes over MQTT"];
        return;
    }
    UTType*                     type   = [UTType typeWithFilenameExtension:@"3mf"] ?: UTTypeData;
    UIDocumentPickerViewController* picker =
        [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[type, UTTypeData]];
    picker.delegate                = self;
    picker.allowsMultipleSelection = NO;
    _pendingPrint                  = print;
    [self presentViewController:picker animated:YES completion:nil];
}

// Confirms where the FTP root actually maps on this printer, which is what the
// print command's file:///sdcard/<name> URL depends on.
- (void)onListSdCard
{
    NSString* ip   = _ipField.text;
    NSString* code = _codeField.text;
    if (ip.length == 0 || code.length == 0) {
        [self log:@"need an IP and an access code"];
        return;
    }
    [self log:@"listing the FTP root over FTPS..."];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        Slic3r::BambuLan::FtpsConfig cfg;
        cfg.host     = ip.UTF8String;
        cfg.port     = 990;
        cfg.username = "bblp";
        cfg.password = code.UTF8String;

        std::string listing, err;
        const int   rc = Slic3r::BambuLan::ftps_list_directory(cfg, "", listing, err);
        if (rc != Slic3r::BambuLan::FtpsOk) {
            [self log:[NSString stringWithFormat:@"listing failed (%d): %s", rc, err.c_str()]];
            return;
        }
        if (listing.empty())
            [self log:@"(the FTP root is empty)"];
        else
            [self log:[NSString stringWithFormat:@"%s", listing.c_str()]];
    });
}

- (void)onSendPrint  { [self pickFileThenPrint:YES]; }
- (void)onUploadOnly { [self pickFileThenPrint:NO]; }

- (void)documentPicker:(UIDocumentPickerViewController*)controller didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls
{
    if (urls.count == 0)
        return;
    const BOOL print = _pendingPrint;

    NSURL* url = urls.firstObject;
    const BOOL scoped = [url startAccessingSecurityScopedResource];
    NSString* local = [NSTemporaryDirectory() stringByAppendingPathComponent:url.lastPathComponent];
    [[NSFileManager defaultManager] removeItemAtPath:local error:nil];
    NSError* error = nil;
    [[NSFileManager defaultManager] copyItemAtURL:url toURL:[NSURL fileURLWithPath:local] error:&error];
    if (scoped)
        [url stopAccessingSecurityScopedResource];
    if (error != nil) {
        [self log:[NSString stringWithFormat:@"cannot read the picked file: %@", error.localizedDescription]];
        return;
    }

    NSString* ip   = _ipField.text;
    NSString* code = _codeField.text;
    NSString* name = url.lastPathComponent;

    [self log:[NSString stringWithFormat:@"uploading %@ over FTPS...", name]];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        Slic3r::BambuLan::FtpsConfig cfg;
        cfg.host     = ip.UTF8String;
        cfg.port     = 990;
        cfg.username = "bblp";
        cfg.password = code.UTF8String;

        int         last_logged = -10;
        std::string err;
        const int   rc = Slic3r::BambuLan::ftps_upload_file(
            cfg, std::string(local.UTF8String), std::string(name.UTF8String),
            [self, &last_logged](int percent) {
                if (percent >= last_logged + 10) {
                    last_logged = percent;
                    [self log:[NSString stringWithFormat:@"  upload %d%%", percent]];
                }
            },
            nullptr, err);

        if (rc != Slic3r::BambuLan::FtpsOk) {
            [self log:[NSString stringWithFormat:@"upload failed (%d): %s", rc, err.c_str()]];
            return;
        }
        [self log:@"upload complete"];
        if (!print)
            return;

        Slic3r::BambuLan::LanPrintRequest req;
        req.file_name    = name.UTF8String;
        req.ftp_folder   = "sdcard/";
        req.plate_index  = 1;
        req.bed_type     = "auto";
        req.sequence_id  = [self nextSequence];
        const json command = json::parse(Slic3r::BambuLan::build_project_file_command(req));
        dispatch_async(dispatch_get_main_queue(), ^{ [self publish:command]; });
    });
}

#endif // BAMBU_LAN_WITH_FTPS

@end

// ---------------------------------------------------------------------------

@interface AppDelegate : UIResponder <UIApplicationDelegate>
@property(strong, nonatomic) UIWindow* window;
@end

@implementation AppDelegate
- (BOOL)application:(UIApplication*)application didFinishLaunchingWithOptions:(NSDictionary*)options
{
    self.window                    = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
    self.window.rootViewController = [[ViewController alloc] init];
    [self.window makeKeyAndVisible];
    return YES;
}
@end

int main(int argc, char* argv[])
{
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([AppDelegate class]));
    }
}
