SERVO_MIN_DEGREE = -90
SERVO_MIN_PULSEWIDTH_US=500
SERVO_MAX_DEGREE=90
SERVO_MAX_PULSEWIDTH_US=2400

def example_angle_to_compare(angle):
  return (angle - SERVO_MIN_DEGREE) * (SERVO_MAX_PULSEWIDTH_US - SERVO_MIN_PULSEWIDTH_US) / (SERVO_MAX_DEGREE - SERVO_MIN_DEGREE) + SERVO_MIN_PULSEWIDTH_US

print(example_angle_to_compare(-90)) # 500.0
print(example_angle_to_compare(0)) # 1450.0
print(example_angle_to_compare(90)) # 2400.0
