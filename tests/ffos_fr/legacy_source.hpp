#ifndef FFOS_LEGACY_SOURCE_HPP
#define FFOS_LEGACY_SOURCE_HPP

// Test-only adapter for immutable, pre-rename source archives. Include after
// their source/declarations; production builds expose only the FFOS interfaces.
#ifndef FFOS_LEGACY_SOURCE
#error "Legacy aliases are only for restored source archives"
#endif

#define FFOS_CMark SWOMark
#define FFOS_CFrontier SWOFrontier
#define FFOS_CControl SWOControl
#define FFOS_CApply SWOApply
#define FFOS_FRControls OFRSuppleControls
#define FFOS_FRControl OFRSuppleControl
#define FFOS_FRApply OFRSuppleApply
#define DecFFOS_C DecSuppleSWO
#define DecFFOS_FR DecOFRSupple

#endif
