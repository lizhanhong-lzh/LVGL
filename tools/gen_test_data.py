import struct

def make_packet(code, value):
    header = b'\x40\x46'
    cmd = 0x0C
    length = 0x06
    style = 0x01
    
    val_bytes = struct.pack('<f', float(value))
    
    # Checksum calculation: XOR of all bytes
    # Header(2) + Cmd(1) + Len(1) + Style(1) + Code(1) + Float(4)
    data = bytearray()
    data.extend(header)
    data.append(cmd)
    data.append(length)
    data.append(style)
    data.append(code)
    data.extend(val_bytes)
    
    chk = 0
    for b in data:
        chk ^= b
        
    data.append(chk)
    
    # Format as hex string space separated
    return ' '.join(f'{b:02X}' for b in data)

# Protocol Codes
DT_INC   = 0x10 # 井斜
DT_AZI   = 0x11 # 方位
DT_GTF   = 0x13 # 重力工具面 (All 0x13 -> Purple)
DT_MTF   = 0x14 # 磁性工具面 (All 0x14 -> Blue)
DT_TEMP  = 0x16 # 温度 (Temperature)
DT_VOLT  = 0x17 # 电池 (Battery)

lines = []

# --- 1. Initialization (Stable Values) ---
# Send these multiple times to ensure connection is detected and values settle
for i in range(5):
    lines.append(make_packet(DT_INC, 25.50))
    lines.append(make_packet(DT_AZI, 138.5))
    lines.append(make_packet(DT_TEMP, 62.0))
    lines.append(make_packet(DT_VOLT, 28.5))

# --- 2. Simulation: Drilling Rotation (GTF Mode) ---
# Rotate from 0 to 360 degrees
# Simulate Drilling: Toolface changes rapidly, Inc/Azi change slowly
inc_base = 25.5
azi_base = 138.5

for angle in range(0, 360, 10): # 10 degree steps
    # Update Toolface (Primary animation)
    lines.append(make_packet(DT_GTF, float(angle)))
    
    # Slowly drift Inclination (simulating drilling curve)
    current_inc = inc_base + (angle / 360.0) * 0.5 # 25.5 -> 26.0
    if angle % 20 == 0: # Update Inc every 2 steps
        lines.append(make_packet(DT_INC, current_inc))
        
    # Slowly drift Azimuth
    current_azi = azi_base + (angle / 360.0) * 1.0 # 138.5 -> 139.5
    if angle % 30 == 0: # Update Azi every 3 steps
        lines.append(make_packet(DT_AZI, current_azi))
        
    # Occasionally update Temp/Volt
    if angle % 90 == 0:
        lines.append(make_packet(DT_TEMP, 62.0 + (angle/1000.0)))
        lines.append(make_packet(DT_VOLT, 28.5 - (angle/2000.0)))

# --- 3. Simulation: Sliding / Steering (MTF Mode) ---
# Switch to Magnetic Toolface (Blue)
# Often used when motor is oriented roughly in one direction
target_mtf = 45.0
for i in range(20): # Verify stable MTF
    # Jitter around 45 degrees
    val = target_mtf + ((i % 3) - 1) * 2.0 
    lines.append(make_packet(DT_MTF, val))
    lines.append(make_packet(DT_INC, 26.0)) # Stable Inc
    lines.append(make_packet(DT_AZI, 140.0))# Stable Azi

# Output the result
print('\n'.join(lines))
