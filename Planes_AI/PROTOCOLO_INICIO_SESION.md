# 🧠 Protocolo de Inicio de Sesión — Physys Lab

> **Instrucción para el agente AI al inicio de cada conversación.**

---

## Comando rápido del usuario:
> "Lee el contexto" o "Carga el proyecto"

## Acciones del agente (en orden):

1. **Verificar grafo**: `Get-Item graphify-out/graph.json | Select Name, Length, LastWriteTime`
   - Si existe (>5MB) → el contexto arquitectónico está intacto.
   - Si no existe → alertar al usuario y ejecutar `graphify update ./`

2. **Leer documentación clave** (en este orden):
   - `Planes_AI/PLAN_IMPLEMENTACION_ACTUALIZADO.md` → Estado general de hitos.
   - `Planes_AI/CHECKLIST_DESARROLLO_V9.md` → Tareas pendientes por sección.
   - `Planes_AI/INFORME_EVOLUCION_PRODUCCION.md` → Último estado de sincronización con `produccion/`.

3. **Verificar estado Git**: `git status --short` + `git log -n 3 --oneline`
   - Confirmar si hay cambios pendientes sin commit.

4. **Reportar al usuario** en formato compacto:
   ```
   ✅ Grafo: OK (XXXX nodos)
   📋 Checklist: X/Y tareas pendientes
   📦 Producción: sincronizada con commit XXXXX
   🔀 Git: limpio / X archivos modificados
   ```

---

## Reglas permanentes:

| Regla | Detalle |
|:---|:---|
| **graphify-out/** | 🔒 PROTEGIDA. Solo usar `graphify update ./`, nunca eliminar. |
| **Planes_AI/** | 📝 No editar sin confirmación explícita del usuario. |
| **produccion/** | 📦 Solo sincronizar cuando el usuario lo solicite. |
| **Formato de docs** | Respetar estructura, iconos y numeración existente. |
