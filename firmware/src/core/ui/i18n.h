#pragma once
// String table (ES/EN), shared by every deck. Add a key to Str and both columns in i18n.cpp. A deck
// whose layout needs another wording (a shorter label, a board-specific hint) overrides that key in
// its own ui/i18n_deck.cpp; every other deck keeps the shared text.

#include "storage/settings_store.h"

namespace ui {

enum class Str : uint16_t {
  AppTitle, NavHome, NavThermals, NavControls, NavSettings, StateBooting, StateNoWifi,
  StateDiscovering, StatePairing, StateConnecting, StateLive, StateStale, StateOffline,
  StateUnauthorized, StateError, StateDemo, DemoBanner, Cpu, Gpu, Ram, Power, CpuPackagePower,
  NoSensor, Load, Temp, Freq, Vram, Used, Chart60s, ChartLoad, ChartTemp, ThermalsTitle, CpuPackage,
  CpuCoreMax, GpuCore, SsdNvme, Max60s, StatusOk, StatusHigh, StatusCritical, NotAvailable,
  ControlsTitle, Volume, Media, LockPc, Shutdown, Restart, SensitiveDisabled, ControlsOffline,
  CmdPending, CmdAck, CmdNack, CmdTimeout, CmdSimulated, ConfirmShutdown, ConfirmRestart, Confirm,
  Cancel, SettingsTitle, Brightness, Language, Alerts, DemoMode, PauseDemo, Diagnostics, Firmware,
  Protocol, HeapInternal, Psram, LastRx, Uptime, Fps, WifiSetupHint, DeviceSection, Never, Seconds,
  Back, DetailSensor, DetailHardware, AlertWarning, AlertCritical, AlertTapToDismiss, WifiTitle,
  WifiAgentButton, Networks, Scan, Scanning, ScanFailed, NoNetworks, OtherNetwork, Password, Ssid,
  Connect, ConnectTo, Forget, NotConnected, ConnectingTo, ConnectedTo, WrongPassword, NetNotFound,
  ConnFailed, Agent, AgentHost, Port, Save, PassLenError, HostError, DemoWillTurnOff, AgentNotSet,
  OpenNetwork, ShowPassword, AgentNoResponse, TempUnavailable, GpuBoardPower, ChartCpu60, NeedsAdmin,
  PairTitle, PairButton, PairSteps, PairFound, PairSearching, PairNoneFound, PairSearchAgain,
  PairManual, PairNoWifi, PairWifiButton, PairEnterCode, PairCodeHint, PairInProgress, PairOk,
  PairErrCode, PairErrUnreachable, PairErrTimeout, PairPairedWith, PairForget, PairNotPaired,
  PairUnauthorized, NothingPlaying, CmdOffline, CmdFailed, PlayerHint, Locking, ShuttingDown,
  Restarting, SystemTitle, PowerHint, SectionConnection, SectionDisplay, SectionOptions, SectionInfo,
  SectionTabs, TabsHint, Battery, BatCharging, BatPlugged, BatOnBattery, BatLeftFmt, DeckBattery,
  DeckOnUsb, PowerRow, Refresh, ScreenOff, DeepSleep, DeepSleepOk, ConfirmDeepSleep, RestartPc,
  ShutdownPc, AutoSleep, NeverOption, AutoSleepSoon, NavToday, SectionWeather, Location, LocationNone,
  Units, ClockFormat, ChoosePlace, PlaceCta, PlaceSubtitle, PlaceSearchHint, PlaceTypeHint,
  PlaceSearching, PlaceNone, PlaceFailed, PlaceOffline, WeatherLoading, WeatherOffline, WeatherFailed,
  WeatherOldApp, Feels, NextHours, NextDays, Now, Sunrise, Sunset, WeatherSource, Change, WxClear,
  WxMostlyClear, WxPartly, WxOvercast, WxFog, WxDrizzle, WxRain, WxHeavyRain, WxShowers, WxSnow,
  WxStorm, WdMon, WdTue, WdWed, WdThu, WdFri, WdSat, WdSun, WdMonL, WdTueL, WdWedL, WdThuL, WdFriL,
  WdSatL, WdSunL, Jan, Feb, Mar, Apr, May, Jun, Jul, Aug, Sep, Oct, Nov, Dec, WizTitle, WizStepFmt,
  WizNext, WizSkip, WizStart, WizStepLang, WizStepPc, WizStepPlace, WizStepDone, WizLangDesc,
  WizWifiPick, WizPairDesc, WizPlaceDesc, WizDoneTitle, WizAlmostTitle, WizDoneHint, WizPartialHint,
  WizExitToast, WizSettingsRow, NavCrypto, FngExtremeFear, FngFear, FngNeutral, FngGreed,
  FngExtremeGreed, CryMarketCap, CryBtcDom, CryFng, CryNoCoins, CryChooseCoins, CryOldApp, CryOffline,
  CryFailed, CryLoading, CryJustNow, CryUpdatedFmt, CryCoinsTitle, CryCoinsHint, CryCoinsCountFmt,
  CryCoinsMaxFmt, CryDone, R1H, R24H, R7D, R1M, R3M, R1Y, RAll, CryHigh24, CryLow24, CryVol24, CryMcap,
  CryAth, CryHighShort, CryLowShort, CryChartLoading, CryChartFailed, SectionCrypto, Currency,
  CryRefresh, ChartStyle, StyleLine, StyleCandles, SleepButton, ButtonScreen, MonJan, MonFeb, MonMar,
  MonApr, MonMay, MonJun, MonJul, MonAug, MonSep, MonOct, MonNov, MonDec, SetupRow, SetupTitle,
  SetupStepFmt, SetupExit, SetupChip, SetupLangDesc, SetupWifiDesc, SetupWifiBtn, SetupPairDesc,
  SetupNeedWifi, SetupCityDesc, SetupNeedPc, SetupCoinsDesc, SetupDoneTitle, SetupDoneDesc, SetupStart,
  SetupNext, SetupSkip, SectionScreensaver, Screensaver, SsAfter, SsBackground, SsFixed, SsRandom,
  SsCycle, SsEvery, SsImage, SsShow, SsWeather, SsPc, SsMusic, SsCrypto, SsDim, SsPreview, SsHint,
  SsPickerTitle, SsPickerHint, SsReplacedBySaver, SsTapToClose, TodayBg, Cities, CityAdd,
  CityAddSubtitle, CitiesHintOne, CitiesHint, CitiesEditHint, CityMain, CityEdit, CityDone, CityTomorrow,
  CityYesterday, CitySameTime, CityFull, SsBattery, SsBatOff, SsBatIcon, SsBatPct,
  Count
};

void set_lang(storage::Lang lang);
storage::Lang lang();
const char *tr(Str id);

// This deck's own wording for a key (src/decks/<DECK>/ui/i18n_deck.cpp), or nullptr: the shared text.
const char *deck_tr(Str id, bool en);

}  // namespace ui
