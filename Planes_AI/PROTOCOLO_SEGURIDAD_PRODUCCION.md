# Protocolo de Seguridad: Actualización de Producción

> [!IMPORTANT]
> **REGLA DE ORO:** Nunca, bajo ninguna circunstancia, se deben actualizar los archivos de la carpeta `produccion/` sin una solicitud previa y explícita del usuario.

## Criterios para la Actualización
1. **Validación de Hitos**: Solo se debe considerar la actualización cuando se han comprobado hitos de desarrollo importantes y estables en el proyecto.
2. **Autorización Directa**: El usuario debe emitir la orden "actualiza producción" o similar después de revisar los cambios en la rama de desarrollo.
3. **Consistencia**: Al actualizar, se debe asegurar que la versión en `produccion/` sea una copia fiel y funcional del estado más estable de la rama principal.

## Registro de Versiones en Producción
- **v9.0 (Actual)**: Sincronizado el 13 de mayo de 2026. Incluye simplificación UX, buses I2C duales, y soporte nativo USB Host MSC.
- **v8.0 (Anterior)**: Versión base estable antes de la reestructuración de la interfaz.

---
*Este documento es una directriz de alta prioridad para la IA y debe ser consultado antes de cualquier operación sobre la carpeta de producción.*
