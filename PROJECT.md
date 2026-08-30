# RP2040 Cycle-Accurate Simulator - Project Master Document

**Simulador científicamente riguroso del Raspberry Pi Pico RP2040 para investigación, validación y tesis**

---

## Documento Maestro

Este es el **documento de entrada** al proyecto. Contiene:
1. Visión general del proyecto
2. Estructura de documentación
3. Cómo usar estos documentos
4. Checklist de implementación
5. Contacto y referencias

---

## Visión General del Proyecto

### Qué es
Un **simulador cycle-accurate** del RP2040 que ejecuta código real (C/C++, ASM, MicroPython) con la misma precisión que el hardware.

### Por qué importa
- El RP2040 tiene un subsistema **PIO (Programmable I/O)** único y complejo
- Los simuladores existentes (QEMU) tienen soporte PIO incompleto/incorrecto
- Este simulador llena ese vacío con **rigor científico** apropiado para tesis de posgrado

### Para quién
- **Investigadores**: validación de algoritmos embebidos sin hardware físico
- **Desarrolladores**: debugging avanzado, traces cycle-accurate
- **Estudiantes**: aprender ARM Cortex-M0+ y PIO en entorno seguro
- **Académicos**: resultados reproducibles y peer-reviewable

### Alcance
| Componente | Cobertura | Fidelidad |
|-----------|----------|----------|
| **CPU** | ARM Cortex-M0+ | 100% (Level 5: Exact) |
| **PIO** | 8 State Machines | 100% (Level 5: Exact) |
| **GPIO** | 28 pines | 99% (Level 4: Precise) |
| **UART** | 2 canales | 99% (Level 4: Precise) |
| **SPI** | 2 canales | 99% (Level 4: Precise) |
| **I2C** | 2 canales | 99% (Level 4: Precise) |
| **Timer** | 4 slices × 2ch | 95% (Level 3: Accurate) |
| **ADC** | 4 canales + temp | 95% (Level 3: Accurate) |
| **Interrupts** | NVIC completa | 100% (Level 5: Exact) |
| **Clock Mgr** | PLL + dividers | 95% (Level 3: Accurate) |

---

## Estructura de Documentación

```
 rp2040-simulator/
│
├─  README.md                  START HERE: Proyecto en 5 min
│  └─ Quick start, features, project overview
│
├─  PROJECT.md                 ESTÁS AQUÍ: Documento maestro
│  └─ Índice, roadmap, cómo usar docs
│
├─  CLAUDE.md                  Para desarrolladores (IA o humanos)
│  └─ Context, constraints, checklist de código
│
├─  DESIGN.md                  Por qué diseñamos así
│  └─ 15 decisiones arquitectónicas, patrones, rationale
│
├─  ARCHITECTURE.md            QUÉ implementar exactamente
│  └─ Specs técnicas: CPU, PIO, memory, periféricos
│
├─  BACKLOG.md                 CUÁNDO y CUÁNTO trabajo
│  └─ 12 sprints, 50+ features, timeline, risks
│
└─  src/                       Código (será creado)
   ├─ core/                     (CPU, memory, clock)
   ├─ pio/                      (State machines, FIFO, ISA)
   ├─ peripherals/              (GPIO, UART, SPI, etc)
   ├─ loaders/                  (ELF, UF2, PIO asm)
   ├─ debuggers/                (GDB stub, profiler)
   └─ simulator.cpp             (Main orchestrator)
```

### Cómo Leer Esta Documentación

**Para diferentes roles:**

#### ‍ Project Manager / Advisor
1. Lee **README.md** (5 min)
2. Lee **BACKLOG.md** - Timeline & milestones (10 min)
3. Lee sección "Fidelity Matrix" en **ARCHITECTURE.md** (5 min)
4. Pregunta: "¿Está on-track con la tesis?"

#### ‍ Desarrollador (Implementación)
1. Lee **CLAUDE.md** completo (constraints, no shortcuts!)
2. Lee **DESIGN.md** (por qué arquitectura)
3. Selecciona feature en **BACKLOG.md**
4. Lee sección correspondiente en **ARCHITECTURE.md**
5. Implementa + prueba + valida

#### Claude / IA Assistant
1. Lee **CLAUDE.md** PRIMERO (crítico para context)
2. Referencia **ARCHITECTURE.md** para specs exactas
3. Usa **DESIGN.md** para decisiones
4. Sigue **BACKLOG.md** para prioridades

#### Revisor de Tesis
1. Lee **README.md** (contexto)
2. Lee **DESIGN.md** (rigor)
3. Lee **ARCHITECTURE.md** (completitud)
4. Revisa **BACKLOG.md** - Fidelity Matrix (validación)
5. Ejecuta tests en `tests/` directorio

#### Estudiante Aprendiendo
1. Lee **README.md** (overview)
2. Lee **CLAUDE.md** secciones "Key Concepts"
3. Lee **ARCHITECTURE.md** secciones de interés (CPU, PIO, etc)
4. Corre ejemplos en `tests/fixtures/`

---

## Quick Start

### Para Entender el Proyecto (15 minutos)
```bash
# 1. Lee esto primero (5 min)
cat README.md

# 2. Entiende el scope (5 min)
grep -A 20 "Fidelity Matrix" ARCHITECTURE.md

# 3. Ve el timeline (5 min)
head -100 BACKLOG.md
```

### Para Implementar (by sprint)
```bash
# Cada semana:
1. Abre BACKLOG.md  Sprint X
2. Lee tareas asignadas
3. Para cada tarea:
   a. Lee ARCHITECTURE.md (specs)
   b. Lee DESIGN.md (rationale)
   c. Implementa
   d. Escribe tests
   e. Valida contra hardware

# Al terminar sprint:
6. Update BACKLOG.md (marca tasks done)
7. Update README.md (progreso)
```

---

## Cómo Este Proyecto Es Diferente

### vs QEMU
```
QEMU:
-  ARM CPU working
-  General periféricos
-  PIO soporte ROTO/INCOMPLETO
-  No diseñado para cycle-accuracy

Nuestro simulador:
-  ARM CPU cycle-accurate
-  Periféricos timing-accurate
-  PIO 100% funcional (8 SM en paralelo)
-  Diseñado desde zero para fidelidad
```

### vs Emuladores Genéricos
```
Emuladores genéricos:
- Rápidos pero imprecisos
- No sirven para tesis
- Debugging limitado

Nuestro simulador:
- Precision + ciclos contados
- Publishable como tesis
- GDB integration + custom debuggers
```

### vs Hardware Real
```
Problema con hardware real:
- Necesitas Picos físicos
- No puedes pausar/inspeccionar fácilmente
- No hay traces de ejecución completas
- Debugging limitado

Nuestro simulador:
- Ilimitados "Picos" virtuales
- Pause, step, inspect en cualquier ciclo
- Traces completes (VCD export)
- Advanced debugging tools
```

---

## Objetivos por Fase

### FASE 1: Core (Semanas 1-2)
**Objetivo**: CPU ARM ejecutando código
- [ ] Register file, pipeline, ISA
- [ ] Memory subsystem (ROM, Flash, SRAM)
- [ ] Basic interrupts
- **Resultado**: Programas ARM simples funcionan

### FASE 2: PIO (Semanas 3-5)  CRÍTICA
**Objetivo**: 8 State Machines en paralelo
- [ ] Toda ISA PIO (9 instrucciones)
- [ ] FIFO bidireccional
- [ ] Clock dividers
- [ ] GPIO integration
- **Resultado**: PIO corre alongside CPU exactamente

### FASE 3: Periféricos (Semanas 6-9)
**Objetivo**: GPIO, Timer, UART, SPI, ADC
- [ ] Timing-accurate implementations
- [ ] Interrupt integration
- [ ] Protocol simulation (UART, SPI)
- **Resultado**: Real firmware runs

### FASE 4: Herramientas (Semana 10)
**Objetivo**: Loaders + Debuggers
- [ ] ELF/UF2 loaders
- [ ] GDB stub
- [ ] PIO debugger
- **Resultado**: Puedo debuggear cualquier programa

### FASE 5: Validación (Semana 11)
**Objetivo**: 200+ tests + Hardware comparison
- [ ] Unit tests (todos los componentes)
- [ ] Integration tests (multi-componente)
- [ ] Hardware validation (vs Picos reales)
- **Resultado**: ±1% accuracy vs hardware

### FASE 6: Documentación (Semana 12)
**Objetivo**: Tesis-ready
- [ ] Code docs + API
- [ ] Technical reports
- [ ] Thesis manuscript
- **Resultado**: Publishable

---

## Checklist de Comprensión

Antes de empezar a implementar, verifica que entiendes:

### Conceptos Fundamentales
- [ ] Qué es un simulador cycle-accurate
- [ ] Diferencia entre CPU y PIO (co-processor)
- [ ] Por qué timing matters en embedded systems
- [ ] Cómo funciona el memory map del RP2040
- [ ] Qué hace cada una de las 9 instrucciones PIO

### Requisitos del Proyecto
- [ ] NO shortcuts (100% fidelidad, no simplificaciones)
- [ ] PIO es el 40% de la complejidad
- [ ] Timing es ±1% exacto
- [ ] Validación con hardware es OBLIGATORIA
- [ ] 200+ tests es requisito

### Arquitectura
- [ ] CPU y PIO corren en paralelo (mismo tick)
- [ ] Memory routing (0x40000000+  periféricos)
- [ ] FIFO blocking behavior
- [ ] Clock divider sin ejecutar SM múltiples veces
- [ ] Interrupt latency = 10-15 ciclos

### Testing
- [ ] Unit tests antes que código (TDD)
- [ ] Hardware comparison obligatoria
- [ ] Coverage >90%
- [ ] Regresión prevención

---

## Key Insights

### Insight 1: PIO No Es GPIO
> "PIO es un co-procesador dedicado con sus propias instrucciones, registros, y flujo de control. No confundas con GPIO."

### Insight 2: Timing Es Todo
> "Un microsegundo = 125 ciclos de CPU. Cada ciclo importa. No hay lugar para 'aproximaciones'."

### Insight 3: Validación Contra Hardware Es Mandatoria
> "Si no lo puedes comparar con un Pico real, no puedes publicarlo como tesis."

### Insight 4: Documentación Es Código
> "Los documentos (DESIGN.md, ARCHITECTURE.md) son tan importantes como el código. Actualiza ambos juntos."

### Insight 5: Sprints Estrictos
> "12 semanas es el límite. No hay feature creep. Si no entra en 12 semanas, va a Phase 2."

---

## Métricas de Éxito

### Ciencia (Para Tesis)
- [ ] Cycle count accuracy: ±0.1%
- [ ] Timing precision: ±10ns (GPIO, UART, SPI)
- [ ] Fidelity matrix documentada
- [ ] Hardware validation: 10+ scenarios
- [ ] Reproducible: deterministic traces

### Ingeniería (Para Código)
- [ ] Code coverage: >90%
- [ ] Tests: 200+
- [ ] Performance: >5x real-time
- [ ] Memory: <500MB runtime
- [ ] Documentation: 100%

### Académica (Para Publicación)
- [ ] Peer-reviewable architecture
- [ ] Methodology documented
- [ ] Limitations listed
- [ ] Comparison tables vs alternatives
- [ ] Reproducible (código disponible)

---

## Cómo Comenzar (Ahora)

### Paso 1: Setup Inicial
```bash
git init rp2040-simulator
cd rp2040-simulator

# Crear estructura
mkdir -p src/{core,pio,peripherals,loaders,debuggers}
mkdir -p include tests/{unit,integration,hardware_cmp,fixtures}
mkdir -p tools docs

# Copiar documentos
cp README.md CLAUDE.md DESIGN.md ARCHITECTURE.md BACKLOG.md .
```

### Paso 2: Entender la Documentación
- [ ] Lee README.md (5 min)
- [ ] Lee CLAUDE.md si eres dev (20 min)
- [ ] Lee DESIGN.md si tienes dudas arquitectónicas (30 min)
- [ ] Referencia ARCHITECTURE.md según necesites (ongoing)
- [ ] Consulta BACKLOG.md para prioridades (5 min weekly)

### Paso 3: Implementar Fase 1
- [ ] Week 1: CPU core (register file, pipeline)
- [ ] Week 2: ISA decoder + memory + basic interrupts
- [ ] End of Week 2: Unit tests passing, simple ARM programs run

### Paso 4: Mantener Documentación
- Cada feature  actualiza ARCHITECTURE.md
- Cada decisión  anota en DESIGN.md
- Cada sprint  update BACKLOG.md progress
- Cada bug  documental en ARCHITECTURE.md limitaciones

---

## Rol de la IA (Claude)

Si uses Claude para ayudar en desarrollo:

### Qué Sí
 "Implementa la instrucción ADD del ARM (datasheet pág 125)"
 "Revisa si mi FIFO blocking está correcto"
 "¿Qué tests debo escribir para SPI?"
 "Genera el boilerplate para un periférico"

### Qué NO
 "Simplifica el PIO"
 "Aproxima el timing"
 "Skip el test para ahorrar tiempo"
 "Genera código sin spec"

### Process
1. Dale a Claude el **CLAUDE.md** completo
2. Para cada tarea, referencia **ARCHITECTURE.md**
3. Pide que valide contra datasheet del RP2040
4. Exige tests ANTES que código
5. Revisa siempre el output

---

## Soporte & Escalation

| Problema | Acción |
|----------|--------|
| ¿Entiendo el scope? | Lee CLAUDE.md, sección "Project Essence" |
| ¿Cómo implemento X? | Busca en ARCHITECTURE.md sección X |
| ¿Por qué diseñamos así? | Mira DESIGN.md, sección "Decision X" |
| ¿Cuál es mi tarea? | Mira BACKLOG.md sprint actual |
| ¿Qué tests escribo? | BACKLOG.md lista tests requeridos |
| ¿Cómo valido? | ARCHITECTURE.md sección "Test Framework" |
| Bloqueado en feature X | Revisa BACKLOG.md dependencies |

---

## Referencias Externas

**Datasheets & Specs:**
- [RP2040 Datasheet](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf) - Official hardware spec
- [ARM Cortex-M0+ Reference](https://developer.arm.com/documentation/100165/0201/) - CPU architecture
- [Thumb ISA](https://developer.arm.com/documentation/ddi0487/) - Instruction set

**Herramientas & Estándares:**
- [GDB Remote Protocol](https://sourceware.org/gdb/current/onlinedocs/gdb/Remote-Protocol.html)
- [VCD Format](https://en.wikipedia.org/wiki/Value_change_dump)
- [ELF Format](https://refspecs.linuxbase.org/elf/elf.pdf)

**Proyectos Relacionados:**
- [pico-sdk](https://github.com/raspberrypi/pico-sdk) - Official RP2040 SDK
- [QEMU ARM](https://qemu.org) - Alternative emulator
- [Renode](https://renode.io) - Multi-platform simulator

---

## Checklist de Lanzamiento

Antes de llamar "DONE" al proyecto:

### Week 11 (Before Testing)
- [ ] Toda documentación updated
- [ ] Código compilable (no warnings)
- [ ] All commits pushed
- [ ] 50+ features implemented

### Week 11-12 (Testing)
- [ ] 200+ tests passing
- [ ] >90% code coverage
- [ ] Hardware validation done
- [ ] No regressions
- [ ] Performance acceptable

### Week 12 (Release)
- [ ] Final documentation review
- [ ] Code review complete
- [ ] Test coverage report generated
- [ ] GitHub repo polished
- [ ] Thesis manuscript draft ready

---

## Versioning & Updates

| Documento | Versión | Última Actualización | Status |
|-----------|---------|---------------------|--------|
| README.md | 1.0 | 2024-08-28 | Stable |
| CLAUDE.md | 1.0 | 2024-08-28 | Stable |
| DESIGN.md | 1.0 | 2024-08-28 | Stable |
| ARCHITECTURE.md | 1.0 | 2024-08-28 | Stable |
| BACKLOG.md | 1.0 | 2024-08-28 | Ready for Phase 1 |
| PROJECT.md | 1.0 | 2024-08-28 | NEW |

**Actualización Semanal**: Cada viernes, revisa y actualiza BACKLOG.md

---

## Para Tu Tesis

### Estructura Sugerida del Manuscript
```
Capítulo 1: Introducción
  ├─ Motivation (PIO es único/complejo)
  ├─ Literature review
  └─ Contributions

Capítulo 2: Background
  ├─ ARM Cortex-M0+
  ├─ PIO architecture
  └─ Related work (QEMU, Renode, etc)

Capítulo 3: Diseño & Arquitectura
  ├─ Simulator architecture (diagrama)
  ├─ Decisiones (ref: DESIGN.md)
  ├─ Validación strategy
  └─ Implementación (módulos principales)

Capítulo 4: Resultados
  ├─ Test coverage
  ├─ Hardware comparison (tablas)
  ├─ Performance metrics
  └─ Fidelity matrix

Capítulo 5: Conclusiones & Future Work
  ├─ Learnings
  ├─ Limitations
  ├─ Phase 2 ideas
  └─ Publications

Apéndices:
  ├─ A: Build & Installation
  ├─ B: API Reference
  ├─ C: Test Cases
  └─ D: Hardware Comparison Data
```

---

## Final Reminders

1. **100% Fidelity, No Shortcuts**
   Esto es una tesis. Calidad > velocidad.

2. **PIO Es Lo Difícil**
   Asigna 40% del esfuerzo aquí. Vale la pena.

3. **Valida Contra Hardware**
   Sin validación hardware, no es publicable.

4. **Documenta Mientras Codeas**
   DESIGN.md y ARCHITECTURE.md son vivos. Actualiza con cada cambio.

5. **Tests Primero**
   Escribe tests antes de código. No exceptions.

6. **Sprints Estrictos**
   12 semanas es el límite. Feature creep = muerte.

---

## Contact

**Author**: Your Name
**Email**: your.email@domain.com
**GitHub**: [@yourusername](https://github.com/yourusername)
**Institution**: Your University

---

## ¿Listo Para Comenzar?

1.  Documentación: HECHA
2.  Siguiente: Comenzar Sprint 1 (Semana 1)
3.  Timeline: 12 semanas
4.  Meta: Simulador thesis-ready

**¡Adelante!**

---

**Document Version**: 1.0
**Last Updated**: 2024-08-28
**Status**: Ready for Phase 1 Launch
**Next Review**: End of Sprint 1
