import codecs
import os

from PyQt4 import QtGui

from Code import Partida
from Code import DBgamesFEN
from Code.QT import Colocacion
from Code.QT import Columnas
from Code.QT import Controles
from Code.QT import Grid
from Code.QT import Iconos
from Code.QT import PantallaPGN
from Code.QT import QTUtil2
from Code.QT import QTVarios
from Code.QT import Voyager
from Code.QT import PantallaSolo
from Code import TrListas
from Code import Util


class WGamesFEN(QtGui.QWidget):
    def __init__(self, procesador, winBookGuide, dbGamesFEN):
        QtGui.QWidget.__init__(self)

        self.winBookGuide = winBookGuide
        self.dbGamesFEN = dbGamesFEN
        self.procesador = procesador
        self.configuracion = procesador.configuracion

        self.infoMove = None  # <-- setInfoMove
        self.numJugada = 0  # Se usa para indicarla al mostrar el pgn en infoMove

        self.terminado = False # singleShot

        self.liFiltro = []
        self.where = None

        # Grid
        oColumnas = Columnas.ListaColumnas()
        oColumnas.nueva("numero", _("N."), 70, siCentrado=True)
        liBasic = dbGamesFEN.liCamposBase
        for clave in liBasic:
            rotulo = TrListas.pgnLabel(clave)
            siCentrado = clave != "EVENT"

            ancho = 140 if clave == "FEN" else 70  # para que sirva con WBG_GamesFEN
            oColumnas.nueva(clave, rotulo, ancho, siCentrado=siCentrado)

        self.grid = Grid.Grid(self, oColumnas, siSelecFilas=True, siSeleccionMultiple=True, xid="wgamesfen")

        # Status bar
        self.status = QtGui.QStatusBar(self)
        self.status.setFixedHeight(22)

        # ToolBar
        liAccionesWork = [
            (_("Close"), Iconos.MainMenu(), self.tw_terminar), None,
            (_("Database"), Iconos.DatabaseC(), self.tg_file), None,
            (_("New"), Iconos.Nuevo(), self.tw_nuevo, _("Add a new game")), None,
            (_("Edit"), Iconos.Modificar(), self.tw_editar), None,
            (_("First"), Iconos.Inicio(), self.tw_gotop), None,
            (_("Last"), Iconos.Final(), self.tw_gobottom), None,
            (_("Filter"), Iconos.Filtrar(), self.tw_filtrar), None,
            (_("Remove"), Iconos.Borrar(), self.tw_borrar),None,
        ]

        self.tbWork = Controles.TBrutina(self, liAccionesWork, tamIcon=24)

        self.lbName = Controles.LB(self, "").ponWrap().alinCentrado().ponColorFondoN("white", "#4E5A65").ponTipoLetra(puntos=16)
        lyNT = Colocacion.H().control(self.lbName)

        lyTB = Colocacion.H().control(self.tbWork)

        layout = Colocacion.V().otro(lyNT).otro(lyTB).control(self.grid).control(self.status).margen(1)

        self.setLayout(layout)

        self.setNameToolBar()

    def limpiaColumnas(self):
        for col in self.grid.oColumnas.liColumnas:
            cab = col.cabecera
            if cab[-1] in "+-":
                col.cabecera = col.antigua

    def setdbGames(self, dbGamesFEN):
        self.dbGamesFEN = dbGamesFEN

    def setInfoMove(self, infoMove):
        self.infoMove = infoMove

    def setNameToolBar(self):
        nomFichero = self.dbGamesFEN.rotulo()
        self.lbName.ponTexto(nomFichero)

    def updateStatus(self):
        if self.terminado:
            return
        self.dbGamesFEN.setFilter(self.where)

    def gridNumDatos(self, grid):
        return self.dbGamesFEN.reccount()

    def gridDato(self, grid, nfila, ocol):
        clave = ocol.clave
        if clave == "numero":
            return str(nfila + 1)
        return self.dbGamesFEN.field(nfila, clave)

    def gridDobleClick(self, grid, fil, col):
        self.tw_editar()

    def gridDobleClickCabecera(self, grid, col):
        liOrden = self.dbGamesFEN.dameOrden()
        clave = col.clave
        if clave == "numero":
            return
        siEsta = False
        for n, (cl, tp) in enumerate(liOrden):
            if cl == clave:
                siEsta = True
                if tp == "ASC":
                    liOrden[n] = (clave, "DESC")
                    col.cabecera = col.antigua + "-"
                    if n:
                        del liOrden[n]
                        liOrden.insert(0, (clave, "DESC"))

                elif tp == "DESC":
                    del liOrden[n]
                    col.cabecera = col.cabecera[:-1]
                break
        if not siEsta:
            liOrden.insert(0, (clave, "ASC"))
            col.antigua = col.cabecera
            col.cabecera = col.antigua + "+"
        self.dbGamesFEN.ponOrden(liOrden)
        self.grid.refresh()
        self.updateStatus()

    def closeEvent(self, event):
        self.tw_terminar()

    def tw_terminar(self):
        self.terminado = True
        self.dbGamesFEN.close()
        self.winBookGuide.terminar()

    def actualiza(self, siObligatorio=False):
        if siObligatorio or self.liFiltro:
            self.where = None
            self.updateStatus()
            self.grid.refresh()
            self.grid.gotop()
        recno = self.grid.recno()
        if recno >= 0:
            self.gridCambiadoRegistro(None, recno, None)

    def gridCambiadoRegistro(self, grid, fila, oCol):
        fen, pv = self.dbGamesFEN.dameFEN_PV(fila)
        p = Partida.Partida(fen=fen)
        p.leerPV(pv)
        p.siTerminada()
        self.infoMove.modoFEN(p, fen, -1)
        self.setFocus()
        self.grid.setFocus()

    def tw_filtrar(self):
        w = PantallaPGN.WFiltrar(self, self.grid.oColumnas, self.liFiltro)
        if w.exec_():
            self.liFiltro = w.liFiltro

            self.where = w.where()
            self.dbGamesFEN.setFilter(self.where)
            self.grid.refresh()
            self.grid.gotop()
            self.updateStatus()

    def tw_gobottom(self):
        self.grid.gobottom()

    def tw_gotop(self):
        self.grid.gotop()

    def tw_nuevo(self):
        # Se genera un PGN
        fen = Voyager.voyagerFEN(self, "", False)
        if fen is not None:
            if self.dbGamesFEN.si_existe_fen(fen):
                QTUtil2.mensError(self, _("This position already exists."))
                return
            hoy = Util.hoy()
            pgn = '[Date "%d.%02d.%02d"]\n[FEN "%s"]' % (hoy.year, hoy.month, hoy.day, fen)

            nuevoPGN, pv, dicPGN = self.procesador.gestorUnPGN(self, pgn)
            if dicPGN:
                liTags = [(clave, valor) for clave, valor in dicPGN.iteritems()]
                partida_completa = Partida.PartidaCompleta(fen=fen, liTags=liTags)
                if pv:
                    partida_completa.leerPV(pv)
                if self.dbGamesFEN.inserta(partida_completa):
                    self.actualiza()
                    self.grid.refresh()
                    self.grid.gobottom()

    def tw_editar(self):
        li = self.grid.recnosSeleccionados()
        if li:
            recno = li[0]
            partidaCompleta = self.dbGamesFEN.leePartidaRecno(recno)

            fen0 = partidaCompleta.iniPosicion.fen()
            partidaCompleta = self.procesador.gestorPartida(self, partidaCompleta, False)
            if partidaCompleta is not None:
                fen1 = partidaCompleta.iniPosicion.fen()
                if fen0 != fen1:
                    if self.dbGamesFEN.si_existe_fen(fen1):
                        QTUtil2.mensError(self, _("This position already exists."))
                        return

                self.dbGamesFEN.guardaPartidaRecno(recno, partidaCompleta)
                self.actualiza()
                self.grid.refresh()
                self.updateStatus()

    def tw_borrar(self):
        li = self.grid.recnosSeleccionados()
        if li:
            if not QTUtil2.pregunta(self, _("Do you want to delete all selected records?")):
                return

            um = QTUtil2.unMomento(self)

            self.dbGamesFEN.borrarLista(li)
            self.grid.refresh()
            self.updateStatus()

            um.final()

    def tg_file(self):
        menu = QTVarios.LCMenu(self)

        menu.opcion(self.tg_createDB, _("Create a new database"), Iconos.NuevaDB())
        menu.separador()
        menu.opcion(self.tg_change, _("Open another database"), Iconos.DatabaseC())
        menu.separador()

        submenu = menu.submenu(_("Import from"), Iconos.DatabaseCNew())
        submenu.opcion(self.tg_importar_PGN, _("A PGN file"), Iconos.FichPGN())
        submenu.separador()
        submenu.opcion(self.tg_importar_DB, _("Other database"), Iconos.DatabaseC())
        submenu.separador()
        submenu.opcion(self.tg_importar_pks, _("A PKS file"), Iconos.JuegaSolo())
        menu.separador()

        submenu = menu.submenu(_("Export to"), Iconos.DatabaseMas())
        submenu.opcion(self.tg_exportar_PGN, _("A PGN file"), Iconos.FichPGN())
        submenu.separador()
        submenu.opcion(self.tg_exportar_DB, _("Other database"), Iconos.DatabaseC())
        menu.separador()

        submenu = menu.submenu(_("Utilities"), Iconos.Utilidades())
        submenu.opcion(self.tg_massive_change_tags, _("Massive change of tags"), Iconos.PGN())

        resp = menu.lanza()
        if resp:
            resp()

    def tg_importar_pks(self):
        path_pks = QTUtil2.leeFichero(self, self.configuracion.dirJS, "pks")
        if path_pks:
            direc = os.path.dirname(path_pks)
            if direc != self.configuracion.dirJS:
                self.configuracion.dirJS = direc
                self.configuracion.graba()

            mens_error = self.dbGamesFEN.insert_pks(path_pks)
            if mens_error:
                QTUtil2.mensError(self, mens_error)
                return
            self.actualiza(True)
            self.grid.gobottom(0)

    def tg_massive_change_tags(self):
        resp = PantallaSolo.massive_change_tags(self, self.configuracion, len(self.grid.recnosSeleccionados()))
        if resp:
            liTags, overwrite, si_all = resp
            liRegistros = range(self.dbGamesFEN.reccount()) if si_all else self.grid.recnosSeleccionados()
            self.dbGamesFEN.massive_change_tags(liTags, liRegistros, overwrite)
            self.actualiza(True)

    def tg_change(self):
        pathFich = QTUtil2.leeFichero(self, os.path.dirname(self.configuracion.ficheroDBgames), "lcf",
                                          _("Positions Database"))
        if pathFich:
            if not pathFich.lower().endswith(".lcf"):
                pathFich += ".lcf"
            path = os.path.dirname(pathFich)
            if os.path.isdir(path):
                self.changeDBgames(pathFich)

    def tg_createDB(self):
        pathFich = QTUtil2.creaFichero(self, os.path.dirname(self.configuracion.ficheroDBgames), "lcf",
                                          _("Positions Database"))
        if pathFich:
            if not pathFich.lower().endswith(".lcf"):
                pathFich += ".lcf"
            path = os.path.dirname(pathFich)
            if os.path.isdir(path):
                Util.borraFichero(pathFich)
                self.changeDBgames(pathFich)

    def tg_exportar(self, ext):
        li = self.grid.recnosSeleccionados()
        if not li:
            return None
        menu = QTVarios.LCMenu(self)
        menu.opcion(True, _("All read"), Iconos.PuntoVerde())
        menu.separador()
        menu.opcion(False, "%s [%d]"%(_("Only selected"),len(li)), Iconos.PuntoAzul())
        siTodos = menu.lanza()
        if siTodos is None:
            return None

        if siTodos:
            li = range(self.dbGamesFEN.reccount())

        # Fichero donde a?adir
        path = QTUtil2.salvaFichero(self, _("Export"), self.configuracion.dirSalvados,
                                      _("File") + " %s (*.%s)"%(ext, ext),
                                      False)
        if path:
            if not path.lower().endswith(".%s"%ext):
                path += ".%s"%ext
            carpeta, nomf = os.path.split(path)
            if carpeta != self.configuracion.dirSalvados:
                self.configuracion.dirSalvados = carpeta
                self.configuracion.graba()

            # Grabamos
            modo = "w"
            if Util.existeFichero(path):
                yn = QTUtil2.preguntaCancelar(self, _X(_("The file %1 already exists, what do you want to do?"), path),
                                              si=_("Append"), no=_("Overwrite"))
                if yn is None:
                    return None
                if yn:
                    modo = "a"
            return li, modo, path
        else:
            return None

    def tg_exportar_DB(self):
        resp = self.tg_exportar("lcf")
        if not resp:
            return
        li, modo, path = resp

        if modo == "w" and Util.existeFichero(path):
            Util.borraFichero(path)

        dlTmp = QTVarios.ImportarFicheroDB(self)
        dlTmp.ponExportados()
        dlTmp.show()

        dbn = DBgamesFEN.DBgamesFEN(path)
        dbn.appendDB(self.dbGamesFEN, li, dlTmp)

    def tg_exportar_PGN(self):
        resp = self.tg_exportar("pgn")
        if not resp:
            return
        li, modo, path = resp

        try:
            fpgn = codecs.open(path, modo, 'utf-8', 'ignore')
        except:
            QTUtil2.mensError(self, "%s : %s\n" % (_("Unable to save"), path))
            return

        pb = QTUtil2.BarraProgreso1(self, _("Exporting..."))
        pb.mostrar()
        total = len(li)
        pb.ponTotal(total)

        if modo == "a":
            fpgn.write("\n\n")
        for n, recno in enumerate(li):
            p = self.dbGamesFEN.leePartidaRecno(recno)
            pb.pon(n + 1)
            if pb.siCancelado():
                break
            fpgn.write(p.pgn())
            fpgn.write("\n\n")

        fpgn.close()
        pb.cerrar()
        QTUtil2.mensaje(self, _X(_("Saved to %1"), path))

    def tg_importar_PGN(self):
        path = QTVarios.select_pgn(self)
        if not path:
            return None

        dlTmp = QTVarios.ImportarFicheroPGN(self)
        dlTmp.show()
        self.dbGamesFEN.leerPGN(path, dlTmp)

        self.actualiza(True)

    def tg_importar_DB(self):
        path = QTVarios.select_ext(self, "lcf")
        if path:
            dlTmp = QTVarios.ImportarFicheroDB(self)
            dlTmp.show()

            dbn = DBgamesFEN.DBgamesFEN(path)
            dbn.lee_rowids()
            liRecnos = range(dbn.reccount())
            self.dbGamesFEN.appendDB(dbn, liRecnos, dlTmp)

            self.actualiza(True)

    def changeDBgames(self, pathFich):
        self.configuracion.ficheroDBgamesFEN = pathFich
        self.configuracion.graba()
        self.winBookGuide.cambiaDBgames(pathFich)
        self.setNameToolBar()
        self.limpiaColumnas()
        self.actualiza(True)

    def tg_create(self):
        resp = self.tg_nombre_depth(_("New"))

        if resp:
            nombre, depth = resp
            self.changeDBgames(nombre)

