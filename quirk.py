from zigpy.quirks.v2 import (
    QuirkBuilder,
    SensorDeviceClass,
    SensorStateClass,
    ReportingConfig
)
from zigpy.quirks.v2.homeassistant import UnitOfMass
from zigpy.zcl.clusters.general import AnalogInput

(
    QuirkBuilder("Moistmaker", "Moisture")
    .sensor(
        "present_value",
        AnalogInput.cluster_id,
        endpoint_id=3,
        device_class=SensorDeviceClass.VOLTAGE,
        state_class=SensorStateClass.MEASUREMENT,
        fallback_name="Moisture",
    )
    .add_to_registry()
)
