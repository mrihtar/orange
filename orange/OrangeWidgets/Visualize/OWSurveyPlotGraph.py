from OWGraph import *
from orngScaleData import *

DONT_SHOW_TOOLTIPS = 0
VISIBLE_ATTRIBUTES = 1
ALL_ATTRIBUTES = 2


class OWSurveyPlotGraph(OWGraph, orngScaleData):
    def __init__(self, parent = None, name = None):
        "Constructs the graph"
        OWGraph.__init__(self, parent, name)
        orngScaleData.__init__(self)
        self.selectedRectangle = 0
        self.exampleTracking = 1
        self.length = 0 # number of shown attributes - we need it also in mouse movement
        self.enabledLegend = 0
        self.tooltipKind = 1
        self.attrLabels = []

    def setData(self, data, **args):
        OWGraph.setData(self, data)
        orngScaleData.setData(self, data, **args)

    #
    # update shown data. Set labels, coloring by className ....
    def updateData(self, labels):
        self.removeCurves()
        self.tips.removeAll()

        self.attrLabels = labels
        self.length = len(labels)
        indices = [self.attributeNameIndex[label] for label in labels]
        #if self.tooltipKind == DONT_SHOW_TOOLTIPS: MyQToolTip.tip(self.tooltip, QRect(0,0,0,0), "")


        if self.noJitteringScaledData == None or len(self.noJitteringScaledData) == 0 or len(labels) == 0:
            self.setAxisScaleDraw(QwtPlot.xBottom, DiscreteAxisScaleDraw(labels))
            self.setAxisScale(QwtPlot.yLeft, 0, 1, 1)
            return

        validData = self.getValidList(indices)
        totalValid = sum(validData)

        self.setAxisScale(QwtPlot.yLeft, 0, totalValid, totalValid)
        self.setAxisScale(QwtPlot.xBottom, -0.5, len(labels)-0.5, 1)
        #self.setAxisMaxMajor(QwtPlot.xBottom, len(labels)-1.0)
        #self.setAxisMaxMinor(QwtPlot.xBottom, 0)
        self.setAxisScaleDraw(QwtPlot.xBottom, DiscreteAxisScaleDraw(labels))
        self.axisScaleDraw(QwtPlot.xBottom).setTickLength(0, 0, 0)  # hide ticks
        self.axisScaleDraw(QwtPlot.xBottom).setOptions(0)           # hide horizontal line representing x axis
        #self.setAxisScale(QwtPlot.yLeft, 0, 1, 1)

        # draw vertical lines that represent attributes
        for i in range(len(labels)):
            newCurveKey = self.insertCurve(labels[i])
            self.setCurveData(newCurveKey, [i,i], [0,1])

        self.repaint()  # we have to repaint to update scale to get right coordinates for tooltip rectangles
        self.updateLayout()

        xRectsToAdd = {}
        yRectsToAdd = {}
        classNameIndex = -1
        if self.rawData.domain.classVar:
            classNameIndex = self.attributeNameIndex[self.rawData.domain.classVar.name]
            if self.rawData.domain.classVar.varType == orange.VarTypes.Discrete:
                classValDict = getVariableValueIndices(self.rawData, self.rawData.domain.classVar)

        y = 0
        for i in range(len(self.rawData)):
            if validData[i] == 0: continue
            if classNameIndex == -1: newColor = (0,0,0)
            elif self.rawData.domain.classVar.varType == orange.VarTypes.Discrete: newColor = self.discPalette.getRGB(classValDict[self.rawData[i].getclass().value])
            else: newColor = self.contPalette.getRGB(self.noJitteringScaledData[classNameIndex][i])

            for j in range(self.length):
                width = self.noJitteringScaledData[indices[j]][i] * 0.45
                if not xRectsToAdd.has_key(newColor):
                    xRectsToAdd[newColor] = []
                    yRectsToAdd[newColor] = []
                xRectsToAdd[newColor].extend([j-width, j+width, j+width, j-width])
                yRectsToAdd[newColor].extend([y, y, y+1, y+1])
            y += 1

        for key in xRectsToAdd.keys():
            self.insertCurve(RectangleCurve(self, QPen(QColor(*key)), QBrush(QColor(*key)), xRectsToAdd[key], yRectsToAdd[key]))

        if self.enabledLegend and self.rawData.domain.classVar and self.rawData.domain.classVar.varType == orange.VarTypes.Discrete:
            classValues = getVariableValuesSorted(self.rawData, self.rawData.domain.classVar.name)
            self.addCurve("<b>" + self.rawData.domain.classVar.name + ":</b>", QColor(0,0,0), QColor(0,0,0), 0, symbol = QwtSymbol.None, enableLegend = 1)
            for ind in range(len(classValues)):
                self.addCurve(classValues[ind], self.discPalette[ind], self.discPalette[ind], 15, symbol = QwtSymbol.Rect, enableLegend = 1)


    # show rectangle with example shown under mouse cursor
    def onMouseMoved(self, e):
        self.hideSelectedRectangle()
        if self.mouseCurrentlyPressed:
            OWGraph.onMouseMoved(self, e)
        elif not self.rawData:
            return
        else:
            yFloat = math.floor(self.invTransform(QwtPlot.yLeft, e.y()))
            if self.exampleTracking:
                width = 0.49
                xData = [-width, self.length+width-1, self.length+width-1, -width, -width]
                yData = [yFloat, yFloat, yFloat+1, yFloat+1, yFloat]
                self.selectedRectangle = self.insertCurve("test")
                self.setCurveData(self.selectedRectangle, xData, yData)
                self.setCurveStyle(self.selectedRectangle, QwtCurve.Lines)
                self.replot()
            else:
                OWGraph.onMouseMoved(self, e)

            if (self.tooltipKind == VISIBLE_ATTRIBUTES and self.attrLabels != []) or self.tooltipKind == ALL_ATTRIBUTES:
                if int(yFloat) >= len(self.rawData): return
                if self.tooltipKind == VISIBLE_ATTRIBUTES:      text = self.getExampleTooltipText(self.rawData, self.rawData[int(yFloat)], self.attrLabels)
                else:                                           text = self.getExampleTooltipText(self.rawData, self.rawData[int(yFloat)], [])
                y1Int = self.transform(QwtPlot.yLeft, yFloat)
                y2Int = self.transform(QwtPlot.yLeft, yFloat+1.0)
                MyQToolTip.tip(self.tooltip, QRect(e.x()+self.canvas().frameGeometry().x()-10, y2Int+self.canvas().frameGeometry().y(), 20, y1Int-y2Int), text)
                OWGraph.onMouseMoved(self, e)


    def hideSelectedRectangle(self):
        if self.selectedRectangle != 0:
            self.removeCurve(self.selectedRectangle)
            self.selectedRectangle = 0


if __name__== "__main__":
    #Draw a simple graph
    a = QApplication(sys.argv)
    c = OWSurveyPlotGraph()

    a.setMainWidget(c)
    c.show()
    a.exec_loop()
