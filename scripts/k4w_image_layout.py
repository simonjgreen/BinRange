"""Pure preflight checks; cryptographic verification is performed by imgtool.

Regions in a layout are (start, size). Programming segments are [start, end).
Call these checks on generated build artifacts, not only source declarations.
"""


def validate_layout(layout, flash_size, sector_size):
    if set(layout) != {'boot', 'primary', 'secondary', 'settings'}:
        raise ValueError('expected boot, primary, secondary and settings regions')
    if flash_size <= 0 or sector_size <= 0:
        raise ValueError('invalid flash geometry')
    previous_end = 0
    for start, size in sorted(layout.values()):
        if start < previous_end or size <= 0 or start % sector_size or size % sector_size:
            raise ValueError('overlapping or unaligned flash region')
        previous_end = start + size
        if previous_end > flash_size:
            raise ValueError('flash region out of bounds')
    if layout['boot'][0] != 0:
        raise ValueError('bootloader must start at zero')
    if layout['secondary'][1] < layout['primary'][1] + sector_size:
        raise ValueError('secondary lacks offset-swap sector')


def validate_image_size(signed_size, primary_size, trailer_reserve):
    if trailer_reserve <= 0 or signed_size <= 0 or signed_size + trailer_reserve > primary_size:
        raise ValueError('signed image and trailer do not fit primary slot')


def validate_segments(segments, allowed):
    if not segments:
        raise ValueError('no programming data')
    previous_end = -1
    for start, end in sorted(segments):
        if start < previous_end or end <= start:
            raise ValueError('overlapping or empty programming segment')
        if not any(low <= start < end <= high for low, high in allowed):
            raise ValueError('programming segment outside permitted flash region')
        previous_end = end


def validate_config(text, required):
    effective = dict(line.split('=', 1) for line in text.splitlines()
                     if line.startswith('CONFIG_') and '=' in line)
    for name, value in required.items():
        if effective.get(name) != value:
            raise ValueError(f'required effective setting missing or incorrect: {name}')
