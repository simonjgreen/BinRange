#pragma once

// Sanitized public fixture based on a read-only capture, with synthetic identity
// and digest values of the original lengths. UWB b100 and wire shapes are kept.
// Includes full SMP v1 headers. Actual responses use indefinite CBOR maps and
// arrays (CONFIG_ZCBOR_CANONICAL=n), including the unknown splitStatus field.
// No PINs, bond keys or signing secrets are present. Hex strings deliberately
// preserve wire bytes without relying on a host CBOR re-encoder.
namespace k4w_smp_fixture {
constexpr char image_request_hex[] = "0000000100010000a0";
constexpr char image_response_hex[] =
    "0100008600010000bf66696d616765739fbf64736c6f74006776657273696f6e65302e322e32"
    "6468617368582000112233445566778899aabbccddeeff00112233445566778899aabbccddeeff"
    "68626f6f7461626c65f56770656e64696e67f469636f6e6669726d6564f566616374697665f5"
    "697065726d616e656e74f4ffff6b73706c697453746174757300ff";
constexpr char status_request_hex[] = "0000000100400100a0";
constexpr char status_response_hex[] =
    "0100007200400100bf62696470663030646261616431323334353637386374616719b100"
    "6776657273696f6e65302e322e3269636f6e6669726d6564f569757074696d655f6d731a00480dd7"
    "6c72657365745f726561736f6e026b6d61696e74656e616e6365f568726164696f5f6f6bf5"
    "66626c655f6f6bf5ff";
} // namespace k4w_smp_fixture
